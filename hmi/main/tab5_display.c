#include "tab5_display.h"

#include "bsp/esp-bsp.h"
#include "bsp/touch.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7121.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "tab5_display";

#define TOUCH_I2C_ADDR_GT911_BACKUP 0x14
#define TOUCH_I2C_ADDR_ST712X       0x55

/* ST7121 panel parameters, taken from the M5Stack Tab5 reference firmware. */
#define ST7121_DSI_LANES          2
#define ST7121_DSI_LANE_RATE_MBPS 965
#define ST7121_DPI_CLOCK_MHZ      70

static tab5_panel_type_t s_panel_type = TAB5_PANEL_UNKNOWN;
static esp_ldo_channel_handle_t s_phy_pwr_chan;

/*
 * Both ST7121 and ST7123 boards answer at 0x55; only the touch firmware major
 * version tells them apart (1 = ST7121, 3 = ST7123).
 */
static tab5_panel_type_t detect_panel(void)
{
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();

    /* Panel and touch share power rails; both must be up and settled before probing. */
    if (bsp_feature_enable(BSP_FEATURE_LCD, true) != ESP_OK ||
        bsp_feature_enable(BSP_FEATURE_TOUCH, true) != ESP_OK) {
        ESP_LOGW(TAG, "Could not enable display power rails");
    }
    vTaskDelay(pdMS_TO_TICKS(500));

    /* The controllers need a while after power-on before they ACK. */
    bool is_gt911 = false;
    bool is_st712x = false;
    for (int attempt = 0; attempt < 15; attempt++) {
        if (i2c_master_probe(bus, TOUCH_I2C_ADDR_GT911_BACKUP, 100) == ESP_OK) {
            is_gt911 = true;
            break;
        }
        if (i2c_master_probe(bus, TOUCH_I2C_ADDR_ST712X, 100) == ESP_OK) {
            is_st712x = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (is_gt911) {
        return TAB5_PANEL_ILI9881C;
    }
    if (!is_st712x) {
        ESP_LOGW(TAG, "No known touch controller found, assuming ILI9881C");
        return TAB5_PANEL_ILI9881C;
    }

    esp_lcd_panel_io_handle_t tp_io = NULL;
    const esp_lcd_panel_io_i2c_config_t tp_io_config = {
        .dev_addr = TOUCH_I2C_ADDR_ST712X,
        .control_phase_bytes = 1,
        .dc_bit_offset = 0,
        .lcd_cmd_bits = 16,
        .flags = {
            .disable_control_phase = 1,
        },
        .scl_speed_hz = 100000,
    };

    tab5_panel_type_t type = TAB5_PANEL_ST7123;
    if (esp_lcd_new_panel_io_i2c(bus, &tp_io_config, &tp_io) == ESP_OK) {
        uint8_t fw_version = 0;
        if (esp_lcd_panel_io_rx_param(tp_io, 0x0000, &fw_version, 1) == ESP_OK) {
            ESP_LOGI(TAG, "Touch firmware version %u", (unsigned)fw_version);
            type = (fw_version == 1) ? TAB5_PANEL_ST7121 : TAB5_PANEL_ST7123;
        }
        esp_lcd_panel_io_del(tp_io);
    }

    return type;
}

static esp_err_t st7121_panel_new(esp_lcd_panel_handle_t *ret_panel, esp_lcd_panel_io_handle_t *ret_io)
{
    const esp_ldo_channel_config_t ldo_cfg = {
        .chan_id = BSP_MIPI_DSI_PHY_PWR_LDO_CHAN,
        .voltage_mv = BSP_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV,
    };
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo_cfg, &s_phy_pwr_chan), TAG, "DSI PHY power failed");

    esp_lcd_dsi_bus_handle_t dsi_bus = NULL;
    const esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0,
        .num_data_lanes = ST7121_DSI_LANES,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = ST7121_DSI_LANE_RATE_MBPS,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus_config, &dsi_bus), TAG, "New DSI bus failed");

    const esp_lcd_dbi_io_config_t dbi_config = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(dsi_bus, &dbi_config, ret_io), TAG, "New panel IO failed");

    const esp_lcd_dpi_panel_config_t dpi_config = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = ST7121_DPI_CLOCK_MHZ,
        .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
        .num_fbs = 1,
        .video_timing = {
            .h_size = BSP_LCD_H_RES,
            .v_size = BSP_LCD_V_RES,
            .hsync_pulse_width = 2,
            .hsync_back_porch = 40,
            .hsync_front_porch = 40,
            .vsync_pulse_width = 20,
            .vsync_back_porch = 24,
            .vsync_front_porch = 200,
        },
        .flags.use_dma2d = true,
    };

    st7121_vendor_config_t vendor_config = {
        .init_cmds = NULL, /* driver default sequence */
        .init_cmds_size = 0,
        .mipi_config = {
            .dsi_bus = dsi_bus,
            .dpi_config = &dpi_config,
        },
    };

    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = -1, /* panel reset is driven by the IO expander */
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
        .bits_per_pixel = 24,
        .vendor_config = &vendor_config,
    };

    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7121(*ret_io, &panel_config, ret_panel), TAG, "New ST7121 panel failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(*ret_panel), TAG, "Panel reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(*ret_panel), TAG, "Panel init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(*ret_panel, true), TAG, "Panel display on failed");

    return ESP_OK;
}

static esp_err_t st7121_display_start(lv_display_t **out_disp)
{
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG, "LVGL port init failed");

    ESP_RETURN_ON_ERROR(bsp_feature_enable(BSP_FEATURE_LCD, true), TAG, "LCD power enable failed");
    ESP_RETURN_ON_ERROR(bsp_display_brightness_init(), TAG, "Brightness init failed");

    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_io_handle_t io = NULL;
    ESP_RETURN_ON_ERROR(st7121_panel_new(&panel, &io), TAG, "ST7121 bring-up failed");

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io,
        .panel_handle = panel,
        .buffer_size = BSP_LCD_H_RES * CONFIG_BSP_LCD_DRAW_BUF_HEIGHT,
        .double_buffer = true,
        .hres = BSP_LCD_H_RES,
        .vres = BSP_LCD_V_RES,
        .monochrome = false,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = true,
            .buff_spiram = false,
            .sw_rotate = true,
        },
    };
    const lvgl_port_display_dsi_cfg_t dpi_cfg = {
        .flags = {
            .avoid_tearing = false,
        },
    };

    lv_display_t *disp = lvgl_port_add_disp_dsi(&disp_cfg, &dpi_cfg);
    ESP_RETURN_ON_FALSE(disp != NULL, ESP_FAIL, TAG, "Adding LVGL display failed");

    esp_lcd_touch_handle_t tp = NULL;
    if (bsp_touch_new(NULL, &tp) == ESP_OK && tp != NULL) {
        const lvgl_port_touch_cfg_t touch_cfg = {
            .disp = disp,
            .handle = tp,
        };
        if (lvgl_port_add_touch(&touch_cfg) == NULL) {
            ESP_LOGW(TAG, "Adding LVGL touch failed");
        }
    } else {
        ESP_LOGW(TAG, "Touch init failed, continuing without input");
    }

    ESP_LOGI(TAG, "ST7121 display started at %dx%d", BSP_LCD_H_RES, BSP_LCD_V_RES);

    if (out_disp) {
        *out_disp = disp;
    }
    return ESP_OK;
}

esp_err_t tab5_display_start(lv_display_t **out_disp)
{
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "I2C init failed");

    s_panel_type = detect_panel();
    ESP_LOGI(TAG, "Detected panel: %s", tab5_display_get_panel_name());

    /* The BSP handles ILI9881C and ST7123 correctly; only ST7121 needs our path. */
    if (s_panel_type != TAB5_PANEL_ST7121) {
        lv_display_t *disp = bsp_display_start();
        ESP_RETURN_ON_FALSE(disp != NULL, ESP_FAIL, TAG, "bsp_display_start failed");
        if (out_disp) {
            *out_disp = disp;
        }
        return ESP_OK;
    }

    return st7121_display_start(out_disp);
}

tab5_panel_type_t tab5_display_get_panel_type(void)
{
    return s_panel_type;
}

const char *tab5_display_get_panel_name(void)
{
    switch (s_panel_type) {
    case TAB5_PANEL_ILI9881C:
        return "ILI9881C";
    case TAB5_PANEL_ST7123:
        return "ST7123";
    case TAB5_PANEL_ST7121:
        return "ST7121";
    default:
        return "unknown";
    }
}
