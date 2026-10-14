// Link test for the ESP32-S3: finds the HVAC node on I2C, then steps through
// a few settings every 4 seconds. Use it to check the wiring before adding
// the hvac_link component to your dash firmware.

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "hvac_link.h"

static const char *TAG = "s3_link_test";

// Change these to the I2C pins your dash uses.
#define SDA_PIN GPIO_NUM_8
#define SCL_PIN GPIO_NUM_9

typedef struct {
    uint8_t fan;
    hvac_mode_t mode;
    uint8_t temp;
} setting_t;

static const setting_t demo[] = {
    {0, HVAC_MODE_FACE, 7},
    {3, HVAC_MODE_FACE, 0},
    {6, HVAC_MODE_FEET, 14},
    {10, HVAC_MODE_DEFROST, 7},
};

void app_main(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = -1,
        .sda_io_num = SDA_PIN,
        .scl_io_num = SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));
    ESP_ERROR_CHECK(hvac_link_init(bus));

    while (hvac_link_probe() != ESP_OK) {
        ESP_LOGW(TAG, "HVAC node not found at 0x%02X, check SDA/SCL/GND and pull-ups", HVAC_I2C_ADDR);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    ESP_LOGI(TAG, "HVAC node found");

    for (int i = 0;; i = (i + 1) % (sizeof(demo) / sizeof(demo[0]))) {
        const setting_t *s = &demo[i];
        esp_err_t err = hvac_link_set(s->fan, s->mode, s->temp);
        ESP_LOGI(TAG, "set fan %u mode %u temp %u: %s", s->fan, s->mode, s->temp, esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(4000));
    }
}
