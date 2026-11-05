#include "odometer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_crc.h"
#include <string.h>
#include "freertos/FreeRTOS.h"

#define ODO_NAMESPACE   "odometer"
#define ODO_KEY_A       "odo_a"
#define ODO_KEY_B       "odo_b"
#define TRIP_A_KEY      "trip_a_m"
#define TRIP_B_KEY      "trip_b_m"

#define SAVE_INTERVAL_M 100

static const char *TAG = "ODOMETER";

typedef struct {
    uint64_t meters;
    uint32_t version;
    uint32_t crc;
} odo_record_t;

static nvs_handle_t odo_handle;
/* 64-bit counters are updated from the odometer task and read/saved/reset
   from other tasks; guard them so readers never see a torn value. */
static portMUX_TYPE s_odo_lock = portMUX_INITIALIZER_UNLOCKED;
#define ODO_LOCK()   portENTER_CRITICAL(&s_odo_lock)
#define ODO_UNLOCK() portEXIT_CRITICAL(&s_odo_lock)

static uint64_t total_meters = 3701491;   // compiled default
static uint64_t last_saved_meters = 0;
static uint32_t current_version = 0;
static bool use_slot_a = true;

/* Trip meters -- simpler than the main odometer's wear-leveled A/B slots
   since these are casual, user-resettable counters rather than a
   legally-meaningful total. Both accumulate alongside the main odometer
   automatically; resetting one never touches the other or the main total. */
static uint64_t trip_a_meters = 0;
static uint64_t trip_b_meters = 0;
static uint64_t trip_a_last_saved = 0;
static uint64_t trip_b_last_saved = 0;

/* ===============================
   CRC Helper
   =============================== */

static uint32_t calculate_crc(const odo_record_t *rec)
{
    return esp_crc32_le(0,
                        (const uint8_t *)rec,
                        sizeof(odo_record_t) - sizeof(uint32_t));
}

static bool record_valid(odo_record_t *rec)
{
    uint32_t crc = calculate_crc(rec);
    return (crc == rec->crc);
}

/* ===============================
   INIT
   =============================== */

void odometer_init(void)
{
    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ESP_ERROR_CHECK(nvs_open(ODO_NAMESPACE, NVS_READWRITE, &odo_handle));

    odo_record_t rec_a = {0};
    odo_record_t rec_b = {0};

    bool valid_a = false;
    bool valid_b = false;

    if (nvs_get_blob(odo_handle, ODO_KEY_A, &rec_a, &(size_t){sizeof(rec_a)}) == ESP_OK)
        valid_a = record_valid(&rec_a);

    if (nvs_get_blob(odo_handle, ODO_KEY_B, &rec_b, &(size_t){sizeof(rec_b)}) == ESP_OK)
        valid_b = record_valid(&rec_b);

    if (valid_a && valid_b) {
        if (rec_a.version >= rec_b.version) {
            total_meters = rec_a.meters;
            current_version = rec_a.version;
            use_slot_a = false;
        } else {
            total_meters = rec_b.meters;
            current_version = rec_b.version;
            use_slot_a = true;
        }
        ESP_LOGI(TAG, "Loaded dual copy: %llu meters (v%u)",
                 total_meters, current_version);
    }
    else if (valid_a) {
        total_meters = rec_a.meters;
        current_version = rec_a.version;
        use_slot_a = false;
        ESP_LOGW(TAG, "Recovered from slot A only");
    }
    else if (valid_b) {
        total_meters = rec_b.meters;
        current_version = rec_b.version;
        use_slot_a = true;
        ESP_LOGW(TAG, "Recovered from slot B only");
    }
    else {
        ESP_LOGW(TAG, "No valid record found. Using default: %llu meters",
                 total_meters);
        current_version = 1;
    }

    last_saved_meters = total_meters;

    int64_t trip_val = 0;
    if (nvs_get_i64(odo_handle, TRIP_A_KEY, &trip_val) == ESP_OK && trip_val >= 0) {
        trip_a_meters = (uint64_t)trip_val;
    }
    if (nvs_get_i64(odo_handle, TRIP_B_KEY, &trip_val) == ESP_OK && trip_val >= 0) {
        trip_b_meters = (uint64_t)trip_val;
    }
    trip_a_last_saved = trip_a_meters;
    trip_b_last_saved = trip_b_meters;
}

/* ===============================
   ADD DISTANCE
   =============================== */

void odometer_add_meters(uint32_t meters)
{
    ODO_LOCK();
    total_meters += meters;
    trip_a_meters += meters;
    trip_b_meters += meters;
    ODO_UNLOCK();
}

/* ===============================
   INTERNAL SAVE
   =============================== */

static void save_record(void)
{
    odo_record_t rec;

    memset(&rec, 0, sizeof(rec));
    ODO_LOCK();
    rec.meters = total_meters;
    ODO_UNLOCK();
    rec.version = current_version + 1;
    rec.crc = calculate_crc(&rec);

    const char *key = use_slot_a ? ODO_KEY_A : ODO_KEY_B;

    /* A failed write (e.g. NVS full) used to abort and reboot the dash every
       100 m; keep running and retry on the next save instead. */
    esp_err_t err = nvs_set_blob(odo_handle, key, &rec, sizeof(rec));
    if (err == ESP_OK) err = nvs_commit(odo_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Odometer save failed: %s", esp_err_to_name(err));
        return;
    }

    current_version = rec.version;
    use_slot_a = !use_slot_a;
    last_saved_meters = rec.meters;

    ESP_LOGI(TAG, "Saved %llu meters (v%u) to %s",
             rec.meters, rec.version, key);
}

/* ===============================
   PERIODIC SAVE
   =============================== */

static void save_trip(const char *key, uint64_t *value, uint64_t *last_saved)
{
    ODO_LOCK();
    uint64_t snapshot = *value;
    ODO_UNLOCK();
    if (nvs_set_i64(odo_handle, key, (int64_t)snapshot) == ESP_OK &&
            nvs_commit(odo_handle) == ESP_OK) {
        *last_saved = snapshot;
    }
}

static void save_trip_a(void) { save_trip(TRIP_A_KEY, &trip_a_meters, &trip_a_last_saved); }
static void save_trip_b(void) { save_trip(TRIP_B_KEY, &trip_b_meters, &trip_b_last_saved); }

void odometer_periodic_save(void)
{
    ODO_LOCK();
    bool total_due = (total_meters - last_saved_meters) >= SAVE_INTERVAL_M;
    bool trip_a_due = (trip_a_meters - trip_a_last_saved) >= SAVE_INTERVAL_M;
    bool trip_b_due = (trip_b_meters - trip_b_last_saved) >= SAVE_INTERVAL_M;
    ODO_UNLOCK();
    if (total_due) save_record();
    if (trip_a_due) save_trip_a();
    if (trip_b_due) save_trip_b();
}

/* ===============================
   FORCE SAVE
   =============================== */

void odometer_force_save(void)
{
    save_record();
    save_trip_a();
    save_trip_b();
}

/* ===============================
   CALIBRATION (absolute set)
   =============================== */

void odometer_set_miles(double miles)
{
    if (miles < 0.0) {
        miles = 0.0;
    }
    ODO_LOCK();
    total_meters = (uint64_t)(miles * 1609.344);
    ODO_UNLOCK();
    save_record();
}

/* ===============================
   TRIP METERS
   =============================== */

void odometer_reset_trip_a(void)
{
    ODO_LOCK();
    trip_a_meters = 0;
    ODO_UNLOCK();
    save_trip_a();
}

void odometer_reset_trip_b(void)
{
    ODO_LOCK();
    trip_b_meters = 0;
    ODO_UNLOCK();
    save_trip_b();
}

/* ===============================
   GETTERS
   =============================== */

uint64_t odometer_get_meters(void)
{
    ODO_LOCK();
    uint64_t value = total_meters;
    ODO_UNLOCK();
    return value;
}

double odometer_get_miles(void)
{
    ODO_LOCK();
    uint64_t value = total_meters;
    ODO_UNLOCK();
    return (double)value / 1609.344;
}

double odometer_get_trip_a_miles(void)
{
    ODO_LOCK();
    uint64_t value = trip_a_meters;
    ODO_UNLOCK();
    return (double)value / 1609.344;
}

double odometer_get_trip_b_miles(void)
{
    ODO_LOCK();
    uint64_t value = trip_b_meters;
    ODO_UNLOCK();
    return (double)value / 1609.344;
}