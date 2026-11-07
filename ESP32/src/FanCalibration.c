///////////////////////////////////////////////////////////////////////////////
// File information

/* Persistent fan characteristic. All protocol, validation, CRC and NVS live
 * here. */
///////////////////////////////////////////////////////////////////////////////
// System includes
#include "nvs.h"
#include "nvs_flash.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////
// FreeRTOS includes
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"

///////////////////////////////////////////////////////////////////////////////
// Application includes
#include "FanCalibration.h"

///////////////////////////////////////////////////////////////////////////////
// File-local data structures

typedef struct
{
    uint32_t velocity, frequency;
} Point;
typedef struct
{
    uint16_t version, count;
    uint32_t crc;
    Point point[FAN_CAL_MAX_POINTS];
} Table;
///////////////////////////////////////////////////////////////////////////////
// File-local variables

static Table active, staging;
static uint32_t received;
static bool uploading;
static int slot = -1;
static bool gate_requested, gate_granted;
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
///////////////////////////////////////////////////////////////////////////////
// FanCalibration_ControlGate()

bool FanCalibration_ControlGate(bool safe_stopped)
{
    portENTER_CRITICAL(&mux);
    gate_granted = gate_requested && safe_stopped;
    bool held = gate_requested;
    portEXIT_CRITICAL(&mux);
    return held;
}
///////////////////////////////////////////////////////////////////////////////
// acquire_gate()

static bool acquire_gate(void)
{
    portENTER_CRITICAL(&mux);
    gate_requested = true;
    gate_granted = false;
    portEXIT_CRITICAL(&mux);
    for (int i = 0; i < 20; i++)
    {
        vTaskDelay(pdMS_TO_TICKS(5));
        portENTER_CRITICAL(&mux);
        bool ok = gate_granted;
        portEXIT_CRITICAL(&mux);
        if (ok)
            return true;
    }
    portENTER_CRITICAL(&mux);
    gate_requested = false;
    portEXIT_CRITICAL(&mux);
    return false;
}
///////////////////////////////////////////////////////////////////////////////
// byte_crc()

static uint32_t byte_crc(uint32_t crc, unsigned byte)
{
    crc ^= byte;
    for (int b = 0; b < 8; b++)
        crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
    return crc;
}
///////////////////////////////////////////////////////////////////////////////
// checksum()

static uint32_t checksum(const Table *t)
{
    uint32_t c = 0xffffffffu;
    for (int i = 0; i < 2; i++)
        c = byte_crc(c, (t->version >> (8 * i)) & 255);
    for (int i = 0; i < 2; i++)
        c = byte_crc(c, (t->count >> (8 * i)) & 255);
    for (unsigned j = 0; j < t->count; j++)
    {
        for (int i = 0; i < 4; i++)
            c = byte_crc(c, (t->point[j].velocity >> (8 * i)) & 255);
        for (int i = 0; i < 4; i++)
            c = byte_crc(c, (t->point[j].frequency >> (8 * i)) & 255);
    }
    return c ^ 0xffffffffu;
}
///////////////////////////////////////////////////////////////////////////////
// valid()

static bool valid(const Table *t)
{
    if (t->version != FAN_CAL_VERSION || t->count < 2 || t->count > FAN_CAL_MAX_POINTS)
        return false;
    for (unsigned i = 0; i < t->count; i++)
    {
        Point p = t->point[i];
        if (!p.velocity || p.velocity > 100000 || !p.frequency || p.frequency > 50000)
            return false;
        if (i &&
            (p.velocity <= t->point[i - 1].velocity || p.frequency <= t->point[i - 1].frequency))
            return false;
    }
    return checksum(t) == t->crc;
}
///////////////////////////////////////////////////////////////////////////////
// publish()

static void publish(const Table *t)
{
    portENTER_CRITICAL(&mux);
    active = *t;
    portEXIT_CRITICAL(&mux);
}
///////////////////////////////////////////////////////////////////////////////
// read_slot()

static bool read_slot(nvs_handle_t h, int s, Table *t)
{
    size_t size = sizeof(*t);
    ///////////////////////////////////////////////////////////////////////////////
    // Function prototypes

    return nvs_get_blob(h, s ? "slot_b" : "slot_a", t, &size) == ESP_OK && size == sizeof(*t) &&
           valid(t);
}
///////////////////////////////////////////////////////////////////////////////
// FanCalibration_Init()

bool FanCalibration_Init(void)
{
    Table t = {0};
    slot = -1;
    uploading = false;
    /* Never erase other commissioning/configuration data on an NVS init error. */
    if (nvs_flash_init() != ESP_OK)
    {
        publish(&t);
        return false;
    }
    nvs_handle_t h;
    if (nvs_open("fan_cal", NVS_READONLY, &h) == ESP_OK)
    {
        uint8_t selected;
        if (nvs_get_u8(h, "active", &selected) == ESP_OK && selected < 2 &&
            read_slot(h, selected, &t))
            slot = selected;
        else
            memset(&t, 0, sizeof(t));
        nvs_close(h);
    }
    publish(&t);
    return slot >= 0;
}
///////////////////////////////////////////////////////////////////////////////
// FanCalibration_GetStatus()

void FanCalibration_GetStatus(FanCalibrationStatus *s)
{
    portENTER_CRITICAL(&mux);
    *s = (FanCalibrationStatus){.valid = active.count >= 2,
                                .points = active.count,
                                .version = FAN_CAL_VERSION,
                                .crc = active.crc};
    portEXIT_CRITICAL(&mux);
}
///////////////////////////////////////////////////////////////////////////////
// FanCalibration_IsValid()

bool FanCalibration_IsValid(void)
{
    FanCalibrationStatus s;
    FanCalibration_GetStatus(&s);
    return s.valid;
}
///////////////////////////////////////////////////////////////////////////////
// FanCalibration_GetBaseFrequency()

float FanCalibration_GetBaseFrequency(float v, float fallback)
{
    Table t;
    portENTER_CRITICAL(&mux);
    t = active;
    portEXIT_CRITICAL(&mux);
    if (!t.count || !isfinite(v) || v < 0)
        return fallback;
    float mm = v * 1000;
    if (mm < t.point[0].velocity)
    {
        /* Blend the physical model into the measured endpoint; zero is not a
         * measured point. */
        float w = mm / t.point[0].velocity;
        return (1 - w) * fallback + w * t.point[0].frequency / 1000.0f;
    }
    for (unsigned i = 1; i < t.count; i++)
        if (mm < t.point[i].velocity)
        {
            Point a = t.point[i - 1], b = t.point[i];
            return (a.frequency +
                    (mm - a.velocity) * (b.frequency - a.frequency) / (b.velocity - a.velocity)) /
                   1000.0f;
        }
    return t.point[t.count - 1].frequency / 1000.0f;
}
///////////////////////////////////////////////////////////////////////////////
// store()

static bool store(void)
{
    nvs_handle_t h;
    if (nvs_open("fan_cal", NVS_READWRITE, &h) != ESP_OK)
        return false;
    int next = slot == 0 ? 1 : 0;
    Table verify;
    bool ok = nvs_set_blob(h, next ? "slot_b" : "slot_a", &staging, sizeof(staging)) == ESP_OK &&
              nvs_commit(h) == ESP_OK && read_slot(h, next, &verify) &&
              memcmp(&verify, &staging, sizeof(verify)) == 0 &&
              nvs_set_u8(h, "active", (uint8_t)next) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    if (ok)
    {
        slot = next;
        publish(&verify);
    }
    return ok;
}
///////////////////////////////////////////////////////////////////////////////
// numbers()

static bool numbers(const char *s, uint32_t *v, int n)
{
    for (int i = 0; i < n; i++)
    {
        if (*s++ != ' ' || *s < '0' || *s > '9')
            return false;
        errno = 0;
        char *end;
        unsigned long long x = strtoull(s, &end, 10);
        if (errno || x > UINT32_MAX)
            return false;
        v[i] = (uint32_t)x;
        s = end;
    }
    return !*s;
}
///////////////////////////////////////////////////////////////////////////////
// FanCalibration_HandleCommand()

bool FanCalibration_HandleCommand(const char *cmd, bool ready, bool stopped, char *out, size_t size)
{
    const char *answer = "ERR CAL_SYNTAX\n";
    uint32_t v[3];
    if (!strcmp(cmd, "CAL_INFO"))
    {
        FanCalibrationStatus s;
        FanCalibration_GetStatus(&s);
        snprintf(out, size,
                 "OK CAL_INFO cal_valid=%d cal_active=%d cal_points=%u cal_crc=%lu "
                 "cal_version=1 cal_source=%s\n",
                 s.valid, s.valid, s.points, (unsigned long)s.crc,
                 s.valid ? "CALIBRATED" : "DEFAULT");
        return true;
    }
    bool held = false;
    if (strcmp(cmd, "CAL_ABORT") && ready && stopped)
    {
        held = acquire_gate();
        ready = held;
    }
    if (!strcmp(cmd, "CAL_ABORT"))
    {
        uploading = false;
        received = 0;
        answer = "OK CAL_ABORT\n";
    }
    else if (!ready || !stopped)
    {
        uploading = false;
        answer = "ERR CAL_READY_STOPPED_REQUIRED\n";
    }
    else if (!strncmp(cmd, "CAL_BEGIN", 9) && numbers(cmd + 9, v, 3))
    {
        if (v[0] == 1 && v[1] >= 2 && v[1] <= FAN_CAL_MAX_POINTS)
        {
            memset(&staging, 0, sizeof(staging));
            staging.version = (uint16_t)v[0];
            staging.count = (uint16_t)v[1];
            staging.crc = v[2];
            received = 0;
            uploading = true;
            answer = "OK CAL_BEGIN\n";
        }
    }
    else if (!strncmp(cmd, "CAL_POINT", 9) && numbers(cmd + 9, v, 3))
    {
        if (uploading && v[0] < staging.count && !(received & (1u << v[0])) && v[1] > 0 &&
            v[1] <= 100000 && v[2] > 0 && v[2] <= 50000)
        {
            staging.point[v[0]] = (Point){v[1], v[2]};
            received |= 1u << v[0];
            answer = "OK CAL_POINT\n";
        }
    }
    else if (!strcmp(cmd, "CAL_COMMIT"))
    {
        if (uploading && received == (UINT32_MAX >> (32 - staging.count)) && valid(&staging))
        {
            answer = store() ? "OK CAL_COMMIT\n" : "ERR CAL_STORAGE\n";
        }
        else
            answer = "ERR CAL_INVALID\n";
        uploading = false;
    }
    else if (!strcmp(cmd, "CAL_RESET"))
    {
        nvs_handle_t h;
        if (nvs_open("fan_cal", NVS_READWRITE, &h) == ESP_OK)
        {
            bool ok = nvs_erase_all(h) == ESP_OK && nvs_commit(h) == ESP_OK;
            nvs_close(h);
            if (ok)
            {
                Table empty = {0};
                publish(&empty);
                slot = -1;
                uploading = false;
                answer = "OK CAL_RESET\n";
            }
            else
                answer = "ERR CAL_STORAGE\n";
        }
        else
            answer = "ERR CAL_STORAGE\n";
    }
    if (held)
    {
        portENTER_CRITICAL(&mux);
        gate_requested = false;
        portEXIT_CRITICAL(&mux);
    }
    snprintf(out, size, "%s", answer);
    return strncmp(answer, "OK", 2) == 0;
}
