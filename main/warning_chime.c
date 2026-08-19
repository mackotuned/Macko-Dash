#include "warning_chime.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "bsp_board_extra.h"
#include "bsp/esp32_p4_wifi6_touch_lcd_4_3.h"
#include "esp_codec_dev_defaults.h"
#include "dash_config.h"
#include "dash_sim.h"

#define WARNING_CHIME_TASK_STACK    4096
#define WARNING_CHIME_TASK_PRIORITY 3
#define WARNING_CHIME_TASK_CORE     1
#define WARNING_CHIME_QUEUE_LEN     8
#define WARNING_CHIME_PATH          "/spiffs/warning_tone.wav"
#define WARNING_CHIME_BOOT_QUIET_MS 9000

static const char *TAG = "WARN_CHIME";

typedef struct {
    uint8_t repeat_count;
    bool test_tone_only;
} warning_chime_event_t;

static QueueHandle_t s_chime_queue = NULL;
static TaskHandle_t s_chime_task = NULL;
static uint32_t s_last_active_warning_mask = 0;
static bool s_audio_ready = false;
static bool s_spiffs_ready = false;
static bool s_audio_unavailable = false;
static bool s_audio_unavailable_logged = false;
static bool s_playback_failed_logged = false;
static bool s_wav_missing_logged = false;
static int s_chime_volume_percent = 60;
static int64_t s_boot_us = 0;
static uint32_t s_test_press_count = 0;
static bool s_test_path_configured = false;
static bool s_prev_sim_enabled = false;

static uint8_t warning_mask_popcount(uint32_t mask)
{
    uint8_t count = 0;
    while (mask) {
        count += (uint8_t)(mask & 1u);
        mask >>= 1;
    }
    return count;
}

static esp_err_t warning_chime_prepare_spiffs(void)
{
    if (s_spiffs_ready) {
        return ESP_OK;
    }

    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 4,
        .format_if_mount_failed = false,
    };

    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        s_spiffs_ready = true;
        return ESP_OK;
    }

    ESP_LOGW(TAG, "SPIFFS mount failed: %s", esp_err_to_name(err));
    return err;
}

static esp_err_t warning_chime_prepare_audio(void)
{
    if (s_audio_unavailable) {
        if (!s_audio_unavailable_logged) {
            ESP_LOGW(TAG, "Audio codec unavailable, warning chime playback suppressed");
            s_audio_unavailable_logged = true;
        }
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (s_audio_ready) {
        return ESP_OK;
    }

    esp_err_t err = bsp_extra_codec_init();
    if (err != ESP_OK) {
        s_audio_unavailable = true;
        s_audio_unavailable_logged = true;
        ESP_LOGW(TAG, "Audio init failed (%s), disabling warning chime audio", esp_err_to_name(err));
        return err;
    }

    err = bsp_extra_player_init();
    if (err != ESP_OK) {
        s_audio_unavailable = true;
        s_audio_unavailable_logged = true;
        ESP_LOGW(TAG, "Player init failed (%s), disabling warning chime audio", esp_err_to_name(err));
        return err;
    }

    err = bsp_extra_codec_volume_set(s_chime_volume_percent, NULL);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Volume set failed: %s", esp_err_to_name(err));
    }
    bsp_extra_codec_mute_set(false);

    s_audio_ready = true;
    return ESP_OK;
}

static bool warning_chime_wav_exists(void)
{
    FILE *f = fopen(WARNING_CHIME_PATH, "rb");
    if (f == NULL) {
        return false;
    }
    fclose(f);
    return true;
}

static esp_err_t warning_chime_recover_audio(void)
{
    ESP_LOGW(TAG, "Audio pipeline reset requested");
    (void)bsp_extra_codec_reset_state();

    s_audio_ready = false;
    s_audio_unavailable = false;
    s_audio_unavailable_logged = false;
    return warning_chime_prepare_audio();
}

static esp_err_t warning_chime_write_silence(uint32_t duration_ms)
{
    if (duration_ms == 0) {
        return ESP_OK;
    }

    const uint32_t sample_rate = CODEC_DEFAULT_SAMPLE_RATE;
    const uint32_t channels = CODEC_DEFAULT_CHANNEL;
    uint32_t remaining_frames = (sample_rate * duration_ms) / 1000u;
    int16_t buffer[256 * CODEC_DEFAULT_CHANNEL] = {0};

    while (remaining_frames > 0) {
        uint32_t chunk_frames = remaining_frames > 256 ? 256 : remaining_frames;
        size_t bytes_written = 0;
        esp_err_t err = bsp_extra_i2s_write(buffer, chunk_frames * channels * sizeof(int16_t),
                                            &bytes_written, portMAX_DELAY);
        if (err != ESP_OK) {
            return err;
        }
        remaining_frames -= chunk_frames;
    }

    return ESP_OK;
}

static esp_err_t warning_chime_write_tone(uint32_t frequency_hz, uint32_t duration_ms)
{
    const uint32_t sample_rate = CODEC_DEFAULT_SAMPLE_RATE;
    const uint32_t channels = CODEC_DEFAULT_CHANNEL;
    const int16_t amplitude = 24000;
    uint32_t remaining_frames = (sample_rate * duration_ms) / 1000u;
    uint32_t half_period = sample_rate / (frequency_hz * 2u);
    uint32_t phase = 0;
    bool high = true;
    int16_t buffer[256 * CODEC_DEFAULT_CHANNEL];

    if (half_period == 0) {
        half_period = 1;
    }

    while (remaining_frames > 0) {
        uint32_t chunk_frames = remaining_frames > 256 ? 256 : remaining_frames;
        for (uint32_t i = 0; i < chunk_frames; ++i) {
            if (phase++ >= half_period) {
                phase = 0;
                high = !high;
            }
            int16_t sample = high ? amplitude : (int16_t)-amplitude;
            for (uint32_t ch = 0; ch < channels; ++ch) {
                buffer[i * channels + ch] = sample;
            }
        }

        size_t bytes_written = 0;
        esp_err_t err = bsp_extra_i2s_write(buffer, chunk_frames * channels * sizeof(int16_t),
                                            &bytes_written, portMAX_DELAY);
        if (err != ESP_OK) {
            return err;
        }
        remaining_frames -= chunk_frames;
    }

    return ESP_OK;
}

static esp_err_t warning_chime_play_fallback_tone(void)
{
    esp_err_t err = warning_chime_write_tone(1320, 80);
    if (err != ESP_OK) {
        return err;
    }
    err = warning_chime_write_silence(35);
    if (err != ESP_OK) {
        return err;
    }
    return warning_chime_write_tone(1760, 120);
}

static esp_err_t warning_chime_play_once(bool test_tone_only)
{
    esp_err_t err = warning_chime_prepare_audio();
    if (err != ESP_OK) {
        return err;
    }

    /* Ensure amplifier gate is asserted before pushing samples. */
    bsp_audio_soft_codec_force_pa(true);

    if (!test_tone_only && warning_chime_prepare_spiffs() == ESP_OK) {
        if (warning_chime_wav_exists()) {
            ESP_LOGI(TAG, "Chime WAV playback start: %s", WARNING_CHIME_PATH);
            err = bsp_extra_player_play_file(WARNING_CHIME_PATH);
            if (err == ESP_OK) {
                return ESP_OK;
            }
            ESP_LOGW(TAG, "WAV chime playback failed (%s), using fallback tone", esp_err_to_name(err));
        } else if (!s_wav_missing_logged) {
            s_wav_missing_logged = true;
            ESP_LOGW(TAG, "WAV file not found: %s (using fallback tone)", WARNING_CHIME_PATH);
        }
    }

    if (test_tone_only) {
        ESP_LOGD(TAG, "Test tone fallback playback start");
    }
    return warning_chime_play_fallback_tone();
}

static void warning_chime_task_main(void *arg)
{
    (void)arg;

    warning_chime_event_t event;
    while (1) {
        if (xQueueReceive(s_chime_queue, &event, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        for (uint8_t i = 0; i < event.repeat_count; ++i) {
            ESP_LOGD(TAG, "Chime worker: event repeat=%u mode=%s", event.repeat_count,
                     event.test_tone_only ? "test-tone" : "wav-first");
            esp_err_t err = warning_chime_play_once(event.test_tone_only);
            if (err != ESP_OK) {
                if (s_audio_unavailable) {
                    break;
                }
                esp_err_t recover_err = warning_chime_recover_audio();
                if (recover_err == ESP_OK) {
                    err = warning_chime_play_once(event.test_tone_only);
                    if (err == ESP_OK) {
                        continue;
                    }
                }
                if (!s_playback_failed_logged) {
                    ESP_LOGW(TAG, "Chime playback failed: %s", esp_err_to_name(err));
                    s_playback_failed_logged = true;
                }
                break;
            }
            if ((i + 1u) < event.repeat_count) {
                warning_chime_write_silence(120);
            }
        }
    }
}

void warning_chime_init(void)
{
    s_chime_volume_percent = dash_config_get_chime_volume();
    if (s_boot_us == 0) {
        s_boot_us = esp_timer_get_time();
    }

    if (s_chime_queue == NULL) {
        s_chime_queue = xQueueCreate(WARNING_CHIME_QUEUE_LEN, sizeof(warning_chime_event_t));
    }
    if (s_chime_task == NULL && s_chime_queue != NULL) {
        xTaskCreatePinnedToCore(warning_chime_task_main, "warning_chime", WARNING_CHIME_TASK_STACK, NULL,
                                WARNING_CHIME_TASK_PRIORITY, &s_chime_task, WARNING_CHIME_TASK_CORE);
    }
}

static void warning_chime_enqueue(uint8_t repeat_count, bool test_tone_only)
{
    warning_chime_init();
    if (s_chime_queue == NULL || repeat_count == 0) {
        return;
    }

    warning_chime_event_t event = {
        .repeat_count = repeat_count,
        .test_tone_only = test_tone_only,
    };
    ESP_LOGD(TAG, "Chime enqueue: repeat=%u mode=%s", repeat_count,
             test_tone_only ? "test-tone" : "wav-first");
    if (xQueueSendToBack(s_chime_queue, &event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Chime queue full, dropping %u event(s)", repeat_count);
    }
}

void warning_chime_process(uint32_t active_warning_mask)
{
    bool sim_enabled = dash_sim_is_enabled();
    if (sim_enabled) {
        s_last_active_warning_mask = active_warning_mask;
        s_prev_sim_enabled = true;
        return;
    }

    if (s_prev_sim_enabled) {
        /* Skip first real frame after simulation to avoid transition false trigger. */
        s_last_active_warning_mask = active_warning_mask;
        s_prev_sim_enabled = false;
        return;
    }

    if (s_boot_us == 0) {
        warning_chime_init();
    }

    if (s_boot_us != 0) {
        int64_t up_ms = (esp_timer_get_time() - s_boot_us) / 1000;
        if (up_ms < WARNING_CHIME_BOOT_QUIET_MS) {
            s_last_active_warning_mask = active_warning_mask;
            return;
        }
    }

    if (s_audio_unavailable) {
        s_last_active_warning_mask = active_warning_mask;
        return;
    }

    warning_chime_init();
    if (s_chime_queue == NULL) {
        s_last_active_warning_mask = active_warning_mask;
        return;
    }

    uint32_t new_warning_mask = active_warning_mask & ~s_last_active_warning_mask;
    uint32_t trigger_mask = new_warning_mask & dash_config_get_chime_warning_mask();
    s_last_active_warning_mask = active_warning_mask;

    uint8_t repeat_count = warning_mask_popcount(trigger_mask);
    if (repeat_count == 0) {
        return;
    }

    warning_chime_enqueue(repeat_count, false);
}

void warning_chime_test(void)
{
    s_test_press_count++;
    if (!s_test_path_configured) {
        bsp_audio_soft_codec_set_pa_active_high(false);
        bsp_audio_soft_codec_set_stc8_audio_sd_active_high(false);
        s_test_path_configured = true;
    }

    int64_t up_ms = 0;
    if (s_boot_us != 0) {
        up_ms = (esp_timer_get_time() - s_boot_us) / 1000;
    }
    ESP_LOGI(TAG, "Test chime requested at %lld ms (press=%lu)",
             (long long)up_ms, (unsigned long)s_test_press_count);

    if (s_audio_unavailable) {
        s_audio_unavailable = false;
        s_audio_unavailable_logged = false;
        s_audio_ready = false;
    }
    warning_chime_enqueue(1, false);
}

void warning_chime_set_volume(int volume_percent)
{
    if (volume_percent < 0) {
        volume_percent = 0;
    }
    if (volume_percent > 100) {
        volume_percent = 100;
    }

    s_chime_volume_percent = volume_percent;
    if (s_audio_ready) {
        esp_err_t err = bsp_extra_codec_volume_set(s_chime_volume_percent, NULL);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Runtime chime volume set failed: %s", esp_err_to_name(err));
        }
    }
}
