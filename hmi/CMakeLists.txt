# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

# Exclude old freemodbus component which exists in older IDF versions
set(EXCLUDE_COMPONENTS freemodbus)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../esp-bist"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(bist_hmi)

# esp_lvgl_port 2.9.0 assumes the post-6.0 MIPI DSI callback name. The IDF 6.0
# release still exposes on_refresh_done, so patch the managed source after the
# component manager has materialized it and before compilation starts.
# IDF_VERSION env/cmake is "6.0.0"; match on major.minor instead.
if(IDF_VERSION_MAJOR EQUAL 6 AND IDF_VERSION_MINOR EQUAL 0)
    set(lvgl_port_disp
        "${CMAKE_CURRENT_LIST_DIR}/managed_components/espressif__esp_lvgl_port/src/lvgl9/esp_lvgl_port_disp.c")
    if(EXISTS "${lvgl_port_disp}")
        file(READ "${lvgl_port_disp}" lvgl_port_disp_contents)
        set(lvgl_port_dsi_old
            "#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)\n            cbs.on_frame_buf_complete = lvgl_port_flush_dpi_vsync_ready_callback;\n#else\n            cbs.on_refresh_done = lvgl_port_flush_dpi_vsync_ready_callback;\n#endif")
        set(lvgl_port_dsi_new
            "cbs.on_refresh_done = lvgl_port_flush_dpi_vsync_ready_callback;")
        string(FIND "${lvgl_port_disp_contents}" "${lvgl_port_dsi_old}" patch_position)
        if(NOT patch_position EQUAL -1)
            string(REPLACE "${lvgl_port_dsi_old}" "${lvgl_port_dsi_new}"
                lvgl_port_disp_patched "${lvgl_port_disp_contents}")
            file(WRITE "${lvgl_port_disp}" "${lvgl_port_disp_patched}")
            message(STATUS "Applied esp_lvgl_port MIPI DSI compatibility patch for IDF 6.0")
        else()
            string(FIND "${lvgl_port_disp_contents}" "${lvgl_port_dsi_new}" patched_position)
        endif()
        if(patch_position EQUAL -1 AND patched_position EQUAL -1)
            message(FATAL_ERROR "esp_lvgl_port IDF 6.0 compatibility patch no longer applies")
        endif()
    endif()
endif()
