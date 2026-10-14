// HVAC node link for the ESP32-S3 dash (ESP-IDF i2c_master driver).
// Copy the whole components/hvac_link folder into your dash project's
// components/ folder.
#pragma once
#include "esp_err.h"
#include "driver/i2c_master.h"
#include "hvac_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

// Attach the HVAC node to an I2C bus your dash already created with
// i2c_new_master_bus(). The node can share the bus with other devices.
esp_err_t hvac_link_init(i2c_master_bus_handle_t bus);

// Check the node answers on the bus.
esp_err_t hvac_link_probe(void);

// Set all three dials. Values are clamped to the valid range.
// Returns ESP_OK when the node acknowledged the packet.
esp_err_t hvac_link_set(uint8_t fan, hvac_mode_t mode, uint8_t temp);

#ifdef __cplusplus
}
#endif
