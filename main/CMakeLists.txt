# -------------------------------------------------------
# Collect protocol JSON files
# -------------------------------------------------------

file(GLOB CAN_PROTOCOL_JSON
    "canbus/protocols/*.json"
)

# -------------------------------------------------------
# Generate protocol list file
# -------------------------------------------------------

set(PROTOCOL_LIST_FILE ${CMAKE_CURRENT_BINARY_DIR}/protocol_list.c)

set(PROTOCOL_LIST_CONTENT "#include \"protocol_loader.h\"\n\n")

# Generate extern declarations
foreach(json_file ${CAN_PROTOCOL_JSON})

    get_filename_component(json_name ${json_file} NAME)

    string(REPLACE "." "_" json_symbol ${json_name})
    string(REPLACE "-" "_" json_symbol ${json_symbol})

    string(APPEND PROTOCOL_LIST_CONTENT "extern const char _binary_${json_symbol}_start[];\n")

endforeach()

string(APPEND PROTOCOL_LIST_CONTENT "\nconst char* protocol_json_list[] = {\n")

# Generate protocol list
foreach(json_file ${CAN_PROTOCOL_JSON})

    get_filename_component(json_name ${json_file} NAME)

    string(REPLACE "." "_" json_symbol ${json_name})
    string(REPLACE "-" "_" json_symbol ${json_symbol})

    string(APPEND PROTOCOL_LIST_CONTENT "    _binary_${json_symbol}_start,\n")

endforeach()

string(APPEND PROTOCOL_LIST_CONTENT "};\n")
string(APPEND PROTOCOL_LIST_CONTENT "const int protocol_json_count = sizeof(protocol_json_list)/sizeof(protocol_json_list[0]);\n")

set(WRITE_PROTOCOL_LIST TRUE)
if(EXISTS ${PROTOCOL_LIST_FILE})
    file(READ ${PROTOCOL_LIST_FILE} EXISTING_PROTOCOL_LIST_CONTENT)
    if("${EXISTING_PROTOCOL_LIST_CONTENT}" STREQUAL "${PROTOCOL_LIST_CONTENT}")
        set(WRITE_PROTOCOL_LIST FALSE)
    endif()
endif()

if(WRITE_PROTOCOL_LIST)
    file(WRITE ${PROTOCOL_LIST_FILE} "${PROTOCOL_LIST_CONTENT}")
endif()

# -------------------------------------------------------
# Component
# -------------------------------------------------------

# -------------------------------------------------------
# WiFi/OTA feature toggle -- when CONFIG_HONDA_DASH_ENABLE_WIFI_OTA
# is off, the wifi-heavy source files and their component
# dependencies are left out of the build entirely (not just
# uncalled). This matters because esp_hosted has its own
# startup constructor that spawns a background SDIO task the
# moment the component is linked in, regardless of whether
# any of our own code calls into it -- so merely skipping a
# function call isn't enough to eliminate the overhead.
# Flip the option back on via menuconfig (C6 OTA Bridge menu)
# to restore the feature; none of the real implementation
# files are touched by this toggle.
# -------------------------------------------------------
if(CONFIG_HONDA_DASH_ENABLE_WIFI_OTA)

idf_component_register(
    SRCS 
        "main.c"
        "boot_logo.c"
        "dash_sim.c"
        "dash_config.c"
        "data_logger.c"
        "device_log_viewer.c"
        "uart_file_transfer.c"
        "session_peaks.c"
        "theme_storage.c"
        "runtime_theme.c"
        "ota_update.c"
        "ota_c6_hosted_bridge.c"
        "honda_dash/honda_dash_ui.c"
        "odometer/odometer.c"
        "canbus/canbus.c"
        "canbus/protocol_loader.c"
        ${PROTOCOL_LIST_FILE}

    INCLUDE_DIRS 
        "./honda_dash"
        "./odometer"
        "./canbus"
        "."

    EMBED_TXTFILES
        ${CAN_PROTOCOL_JSON}

    REQUIRES
        esp32_p4_wifi6_touch_lcd_4_3
        esp_driver_twai
        esp_driver_uart
        esp_app_format
        esp_event
        esp_netif
        esp_wifi
        esp_wifi_remote
        esp_http_server
        nvs_flash
        esp_hosted
        spiffs
        json
        espressif__zlib
)

else()

idf_component_register(
    SRCS 
        "main.c"
        "boot_logo.c"
        "dash_sim.c"
        "dash_config.c"
        "data_logger.c"
        "device_log_viewer.c"
        "uart_file_transfer.c"
        "session_peaks.c"
        "theme_storage.c"
        "runtime_theme.c"
        "ota_update_stub.c"
        "honda_dash/honda_dash_ui.c"
        "odometer/odometer.c"
        "canbus/canbus.c"
        "canbus/protocol_loader.c"
        ${PROTOCOL_LIST_FILE}

    INCLUDE_DIRS 
        "./honda_dash"
        "./odometer"
        "./canbus"
        "."

    EMBED_TXTFILES
        ${CAN_PROTOCOL_JSON}

    REQUIRES
        esp32_p4_wifi6_touch_lcd_4_3
        esp_driver_twai
        esp_driver_uart
        esp_app_format
        esp_event
        esp_netif
        nvs_flash
        spiffs
        json
        espressif__zlib
)

endif()