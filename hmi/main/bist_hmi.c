#include <inttypes.h>
#include <stdio.h>

#include "bsp/esp-bsp.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "hmi_ui.h"
#include "serial_master.h"
#include "tab5_display.h"
#include "ulp_lp_core.h"
#include "lp_core_uart.h"
#include "bist_hd_agent.h"
#include "bist_hd_protocol.h"

#define MAILBOX_TIMEOUT_MS  10000

static const char *TAG = "bist_hmi";

extern const uint8_t ulp_idf_bist_sample_bin_start[] asm("_binary_ulp_idf_bist_sample_bin_start");
extern const uint8_t ulp_idf_bist_sample_bin_end[] asm("_binary_ulp_idf_bist_sample_bin_end");

static lv_display_t *s_disp;
static QueueHandle_t s_command_queue;

typedef enum {
    MOTOR_COMMAND_SET_POWER,
    MOTOR_COMMAND_SET_SPEED,
} motor_command_type_t;

typedef struct {
    motor_command_type_t type;
    uint16_t value;
} motor_command_t;

/*
 * The Host Diagnostic Agent owns mailbox I/O on a high-priority worker
 * (AGENT_READY, LP_STATUS drain, optional Q&A). This task only consumes
 * queued LP status for the UI and must stay free of LVGL/Modbus work so
 * post-boot / runtime results are reflected promptly.
 */
static volatile bool s_bist_post_boot_ok = false;
static volatile bool s_bist_post_boot_done = false;
static volatile bool s_bist_runtime_ok = false;
static volatile bool s_hmi_halted = false;

static void lp_uart_init(void)
{
    lp_core_uart_cfg_t cfg = LP_CORE_UART_DEFAULT_CONFIG();
    cfg.uart_pin_cfg.tx_io_num = GPIO_NUM_2;
    cfg.uart_pin_cfg.rx_io_num = GPIO_NUM_3;

    ESP_ERROR_CHECK(lp_core_uart_init(&cfg));

    ESP_LOGI(TAG, "LP UART initialized successfully");
}

static void lp_core_init(void)
{
    ulp_lp_core_cfg_t cfg = {
        .wakeup_source = ULP_LP_CORE_WAKEUP_SOURCE_HP_CPU,
    };

    /* An HP-only reset leaves the LP core running: stop it so the companion
     * restarts from scratch instead of racing the new agent handshake. */
    ulp_lp_core_stop();

    ESP_ERROR_CHECK(ulp_lp_core_load_binary(ulp_idf_bist_sample_bin_start,
                    (ulp_idf_bist_sample_bin_end - ulp_idf_bist_sample_bin_start)));

    ESP_ERROR_CHECK(ulp_lp_core_run(&cfg));

    ESP_LOGI(TAG, "LP core loaded with firmware and running successfully");
}

static void print_postboot_results(uint32_t result)
{
    printf("=== Post-boot BIST results ===\n");
    printf("test_BIST_cpu_reg:%s\n",     (result & BIST_HD_BIT_CPU_REG) ? "PASS" : "FAIL");
    printf("test_BIST_cpu_csr:%s\n",     (result & BIST_HD_BIT_CPU_CSR) ? "PASS" : "FAIL");
    printf("test_BIST_ram_march_x:%s\n", (result & BIST_HD_BIT_RAM_X)  ? "PASS" : "FAIL");
    printf("test_BIST_ram_abraham:%s\n", (result & BIST_HD_BIT_ABRAHAM) ? "PASS" : "FAIL");
    printf("test_BIST_flash_crc:%s\n",   (result & BIST_HD_BIT_FLASH)  ? "PASS" : "FAIL");
}

static void print_runtime_results(uint32_t result)
{
    printf("=== Runtime BIST results ===\n");
    printf("test_BIST_runtime_cpu_reg:%s\n",      (result & BIST_HD_BIT_CPU_REG) ? "PASS" : "FAIL");
    printf("test_BIST_runtime_cpu_csr:%s\n",      (result & BIST_HD_BIT_CPU_CSR) ? "PASS" : "FAIL");
    printf("test_BIST_runtime_ram_march_a:%s\n",  (result & BIST_HD_BIT_RAM_A)   ? "PASS" : "FAIL");
    printf("test_BIST_runtime_ram_abraham:%s\n",  (result & BIST_HD_BIT_ABRAHAM) ? "PASS" : "FAIL");
    printf("test_BIST_runtime_stack_check:%s\n",  (result & BIST_HD_BIT_STACK)   ? "PASS" : "FAIL");
}

static void update_bist_runtime_status(void)
{
    bsp_display_lock(0);
    hmi_ui_set_bist_runtime(s_bist_runtime_ok);
    bsp_display_unlock();
}

static void update_bist_postboot_status(void)
{
    bsp_display_lock(0);
    hmi_ui_set_bist_postboot(s_bist_post_boot_ok);
    bsp_display_unlock();
}

/*
 * Runs on the main (UI) task after the status task flags a runtime failure.
 * Puts the motor in a safe state and marks the UI as stopped. Mailbox I/O
 * stays with the Host Diagnostic Agent worker.
 */
static void enter_hmi_safe_state(void)
{
    ESP_LOGE(TAG, "Runtime BIST failed — stopping HMI");

    /* Best-effort safe state: cut motor power and ignore further UI commands. */
    (void)master_write_u8(CID_MOTOR_ON_OFF, 0);
    if (s_command_queue) {
        xQueueReset(s_command_queue);
    }

    bsp_display_lock(0);
    hmi_ui_set_halted();
    bsp_display_unlock();
}

/*
 * Consumes LP_STATUS words queued by the Host Diagnostic Agent worker.
 * Post-boot is one-shot; runtime status is polled forever for the UI.
 */
static void bist_status_task(void *arg)
{
    (void)arg;
    uint32_t status;
    int err;

    err = bist_hd_agent_wait_lp_status(&status, BIST_HD_BIT_POSTBOOT, MAILBOX_TIMEOUT_MS);
    if (err == 0) {
        print_postboot_results(status);
        s_bist_post_boot_ok = (status & BIST_HD_POSTBOOT_ALL_PASS) == BIST_HD_POSTBOOT_ALL_PASS;
    } else {
        ESP_LOGE(TAG, "BIST post-boot wait failed: %d", err);
        s_bist_post_boot_ok = false;
    }
    s_bist_post_boot_done = true;

    ESP_LOGI(TAG, "BIST post-boot done");

    for (;;) {
        err = bist_hd_agent_wait_lp_status(&status, BIST_HD_BIT_RUNTIME, MAILBOX_TIMEOUT_MS);
        if (err != 0) {
            ESP_LOGE(TAG, "BIST runtime wait failed: %d", err);
            s_bist_runtime_ok = false;
            s_hmi_halted = true;
            continue;
        }

        bool pass = (status & BIST_HD_RUNTIME_ALL_PASS) == BIST_HD_RUNTIME_ALL_PASS;
        s_bist_runtime_ok = pass;
        if (!pass) {
            print_runtime_results(status);
            s_hmi_halted = true;
        }
    }
}

static void queue_motor_command(motor_command_type_t type, uint16_t value)
{
    const motor_command_t command = {
        .type = type,
        .value = value,
    };

    if (s_hmi_halted) {
        ESP_LOGW(TAG, "Ignoring motor command — HMI halted");
        return;
    }

    if (s_command_queue == NULL) {
        ESP_LOGW(TAG, "Ignoring motor command — queue not ready");
        return;
    }

    if (xQueueSend(s_command_queue, &command, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Motor command queue is full");
    }
}

static void on_ui_power(bool power_on, void *user_data)
{
    (void)user_data;
    queue_motor_command(MOTOR_COMMAND_SET_POWER, power_on ? 1 : 0);
}

static void on_ui_speed(uint16_t target_speed_rpm, void *user_data)
{
    (void)user_data;
    queue_motor_command(MOTOR_COMMAND_SET_SPEED, target_speed_rpm);
}

static esp_err_t display_init(void)
{
    ESP_LOGI(TAG, "Initializing Tab5 display");
    ESP_RETURN_ON_ERROR(tab5_display_start(&s_disp), TAG, "Display bring-up failed");

    bsp_display_rotate(s_disp, LV_DISPLAY_ROTATION_90);
    ESP_ERROR_CHECK(bsp_display_backlight_on());

    const hmi_ui_ops_t ui_ops = {
        .on_power = on_ui_power,
        .on_speed = on_ui_speed,
        .user_data = NULL,
        .initial_target_speed = 1000,
    };

    bsp_display_lock(0);
    hmi_ui_create(lv_display_get_screen_active(s_disp), &ui_ops);
    bsp_display_unlock();

    ESP_LOGI(TAG, "Display ready");
    return ESP_OK;
}

static void update_motor_status(bool init)
{
    uint8_t motor_on = 0;
    uint16_t current_speed = 0;
    uint16_t temperature = 0;
    uint8_t overspeed_alarm = 0;
    uint16_t speed_setpoint = 0;

    if (master_read_u8(CID_MOTOR_ON_OFF, &motor_on) != ESP_OK ||
        master_read_u16(CID_MOTOR_CURRENT_SPEED, &current_speed) != ESP_OK ||
        master_read_u16(CID_MOTOR_TEMPERATURE, &temperature) != ESP_OK ||
        master_read_u8(CID_OVERSPEED_ALARM, &overspeed_alarm) != ESP_OK ||
        master_read_u16(CID_MOTOR_SPEED_SETPOINT, &speed_setpoint) != ESP_OK) {
        ESP_LOGE(TAG, "Could not read complete motor status");
        bsp_display_lock(0);
        hmi_ui_set_motor_unavailable();
        hmi_ui_set_overspeed_unknown();
        bsp_display_unlock();
        return;
    }

    ESP_LOGI(TAG, "Motor status: on=%u speed=%u RPM temp=%u C overspeed=%u setpoint=%u RPM",
             (unsigned)motor_on,
             (unsigned)current_speed,
             (unsigned)temperature,
             (unsigned)overspeed_alarm,
             (unsigned)speed_setpoint);

    bsp_display_lock(0);
    hmi_ui_set_motor_status(current_speed, temperature);
    hmi_ui_set_overspeed(overspeed_alarm != 0);
    if (init) {
        hmi_ui_set_target_speed(speed_setpoint);
    }
    bsp_display_unlock();
}

static void execute_motor_command(const motor_command_t *command)
{
    esp_err_t err;

    if (command->type == MOTOR_COMMAND_SET_POWER) {
        err = master_write_u8(CID_MOTOR_ON_OFF, (uint8_t)command->value);
        ESP_LOGI(TAG, "Motor power command: %s", command->value ? "ON" : "OFF");
    } else {
        err = master_write_u16(CID_MOTOR_SPEED_SETPOINT, command->value);
        ESP_LOGI(TAG, "Motor target speed command: %u RPM", (unsigned)command->value);
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Motor command failed: %s", esp_err_to_name(err));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting BIST HMI");

    vTaskDelay(pdMS_TO_TICKS(1000));

    uint32_t causes = esp_sleep_get_wakeup_causes();
    if (!(causes & BIT(ESP_SLEEP_WAKEUP_ULP))) {
        ESP_LOGI(TAG, "Not an LP core wakeup. Causes = 0x%" PRIx32, causes);
        ESP_LOGI(TAG, "Initializing...");

        lp_uart_init();
        lp_core_init();
    }

    /* Give the LP companion time to initialize the software mailbox first. */
    vTaskDelay(pdMS_TO_TICKS(100));

    /*
     * Start agent + status task before display/Modbus. Companion AGENT_READY
     * timeout is short; the worker must be live before post-boot LP_STATUS
     * so it can ACK with AGENT_POSTBOOT_DONE.
     */
    int agent_err = bist_hd_agent_start();
    if (agent_err != 0) {
        ESP_LOGE(TAG, "Failed to start Host Diagnostic Agent: %d", agent_err);
        ESP_ERROR_CHECK(ESP_FAIL);
    }
    ESP_LOGI(TAG, "Host Diagnostic Agent started");

    BaseType_t task_ok = xTaskCreate(bist_status_task, "bist_status", 4096, NULL,
                                     configMAX_PRIORITIES - 2, NULL);
    ESP_ERROR_CHECK(task_ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);

    /* Wait for the post-boot result before enabling motor control. */

    while (!s_bist_post_boot_done) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    /* Initialize the display after the post-boot handshake completes. */
    ESP_ERROR_CHECK(display_init());

    if (!s_bist_post_boot_ok) {
        ESP_LOGE(TAG, "Post-boot BIST failed");
        return;
    }

    s_command_queue = xQueueCreate(8, sizeof(motor_command_t));
    ESP_ERROR_CHECK(s_command_queue ? ESP_OK : ESP_ERR_NO_MEM);

    /* Start the Modbus master. */

    ESP_ERROR_CHECK(master_init());

    update_motor_status(true);
    update_bist_postboot_status();

    bool runtime_rendered = false;
    bool safe_state_entered = false;
    bool last_runtime_ok = false;
    TickType_t last_status_update = 0;
    for (;;) {
        /* Reflect the latest runtime BIST status produced by the status task. */
        if (!runtime_rendered || s_bist_runtime_ok != last_runtime_ok) {
            last_runtime_ok = s_bist_runtime_ok;
            runtime_rendered = true;
            update_bist_runtime_status();
        }

        if (s_hmi_halted) {
            if (!safe_state_entered) {
                enter_hmi_safe_state();
                safe_state_entered = true;
            }
            /* Agent worker keeps the LP mailbox drained; idle the UI here. */
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        motor_command_t command;
        if (xQueueReceive(s_command_queue, &command, 0) == pdTRUE) {
            execute_motor_command(&command);
        }

        if (xTaskGetTickCount() - last_status_update >= pdMS_TO_TICKS(1000)) {
            update_motor_status(false);
            last_status_update = xTaskGetTickCount();
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
