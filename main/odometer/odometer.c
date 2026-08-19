#include "odometer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_crc.h"
#include <string.h>

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
    total_meters += meters;
    trip_a_meters += meters;
    trip_b_meters += meters;
}

/* ===============================
   INTERNAL SAVE
   =============================== */

static void save_record(void)
{
    odo_record_t rec;

    rec.meters = total_meters;
    rec.version = ++current_version;
    rec.crc = calculate_crc(&rec);

    const char *key = use_slot_a ? ODO_KEY_A : ODO_KEY_B;

    ESP_ERROR_CHECK(nvs_set_blob(odo_handle, key, &rec, sizeof(rec)));
    ESP_ERROR_CHECK(nvs_commit(odo_handle));

    use_slot_a = !use_slot_a;
    last_saved_meters = total_meters;

    ESP_LOGI(TAG, "Saved %llu meters (v%u) to %s",
             total_meters, rec.version, key);
}

/* ===============================
   PERIODIC SAVE
   =============================== */

static void save_trip_a(void)
{
    nvs_set_i64(odo_handle, TRIP_A_KEY, (int64_t)trip_a_meters);
    nvs_commit(odo_handle);
    trip_a_last_saved = trip_a_meters;
}

static void save_trip_b(void)
{
    nvs_set_i64(odo_handle, TRIP_B_KEY, (int64_t)trip_b_meters);
    nvs_commit(odo_handle);
    trip_b_last_saved = trip_b_meters;
}

void odometer_periodic_save(void)
{
    if ((total_meters - last_saved_meters) >= SAVE_INTERVAL_M)
    {
        save_record();
    }
    if ((trip_a_meters - trip_a_last_saved) >= SAVE_INTERVAL_M) {
        save_trip_a();
    }
    if ((trip_b_meters - trip_b_last_saved) >= SAVE_INTERVAL_M) {
        save_trip_b();
    }
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
    total_meters = (uint64_t)(miles * 1609.344);
    save_record();
}

/* ===============================
   TRIP METERS
   =============================== */

void odometer_reset_trip_a(void)
{
    trip_a_meters = 0;
    save_trip_a();
}

void odometer_reset_trip_b(void)
{
    trip_b_meters = 0;
    save_trip_b();
}

/* ===============================
   GETTERS
   =============================== */

uint64_t odometer_get_meters(void)
{
    return total_meters;
}

double odometer_get_miles(void)
{
    return (double)total_meters / 1609.344;
}

double odometer_get_trip_a_miles(void)
{
    return (double)trip_a_meters / 1609.344;
}

double odometer_get_trip_b_miles(void)
{
    return (double)trip_b_meters / 1609.344;
}