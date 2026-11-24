#include "ModbusTask.h"
#include "AppConfig.h"
#include "ApplicationTasks.h"
#include "SafetyTime.h"
#include "SafetyIO.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <math.h>

/* Single ControlTask producer, single ModbusTask consumer. Disarm cancels
 * pending arm; reset is a distinct physical-reset action. All driver calls
 * are made by ModbusTask. Simulation injection has its own driver lock. */
typedef struct {
    bool disarm, reset, arm;
    uint32_t epoch;
    int64_t arm_us;
} Actions;
typedef struct {
    MotorStatus motor;
    int64_t published_us, start_proof_us, stop_proof_us;
} Snapshot;
static portMUX_TYPE action_lock = portMUX_INITIALIZER_UNLOCKED;
static Actions actions;
static QueueHandle_t snapshots;
static Snapshot owner_snapshot;

bool ModbusTask_CreateInterfaces(void)
{
    snapshots = xQueueCreate(1, sizeof(Snapshot));
    return snapshots != NULL;
}

static bool read_snapshot(Snapshot *s)
{
    return snapshots && xQueuePeek(snapshots, s, 0) == pdTRUE &&
        esp_timer_get_time() - s->published_us < MODBUS_TASK_SNAPSHOT_MAX_AGE_US;
}
void ModbusTask_GetStatus(MotorStatus *status)
{
    Snapshot s = {0};
    if (read_snapshot(&s)) *status = s.motor;
    else *status = (MotorStatus){.run_locked = true};
}
bool ModbusTask_Arm(uint32_t epoch)
{
    Snapshot s = {0};
    if (!read_snapshot(&s) || !s.motor.ok || !s.motor.ready || s.motor.run_locked)
        return false;
    portENTER_CRITICAL(&action_lock);
    actions.arm = true;
    actions.epoch = epoch;
    actions.arm_us = esp_timer_get_time();
    portEXIT_CRITICAL(&action_lock);
    return true;
}
void ModbusTask_Disarm(void)
{
    portENTER_CRITICAL(&action_lock);
    actions.disarm = true;
    actions.arm = false;
    portEXIT_CRITICAL(&action_lock);
}
void ModbusTask_Reset(void)
{
    portENTER_CRITICAL(&action_lock);
    actions.disarm = true;
    actions.arm = false;
    actions.reset = true;
    portEXIT_CRITICAL(&action_lock);
}
bool ModbusTask_StartConfirmed(int64_t entered_us)
{
    Snapshot s = {0};
    return read_snapshot(&s) && s.start_proof_us > 0 && s.start_proof_us >= entered_us;
}
bool ModbusTask_StopConfirmed(int64_t entered_us, float maximum_hz)
{
    Snapshot s = {0};
    return read_snapshot(&s) && s.stop_proof_us > 0 && s.stop_proof_us >= entered_us &&
        isfinite(maximum_hz) && maximum_hz >= 0 && fabsf(s.motor.actual_hz) <= maximum_hz;
}

void ModbusTask(void *argument)
{
    (void)argument;
    bool initialized = false, pending_arm = false, rejected = false;
    uint32_t arm_epoch = 0;
    int64_t arm_us = 0, last_init_us = -1000000;
    const TickType_t period = pdMS_TO_TICKS(MODBUS_TASK_PERIOD_MS) ? pdMS_TO_TICKS(MODBUS_TASK_PERIOD_MS) : 1;
    ESP_LOGI("ModbusTask", "baud=%d service_ticks=%u", MODBUS_BAUD, (unsigned)period);
    for (;;) {
        int64_t now = esp_timer_get_time();
        if (!initialized && now - last_init_us >= 1000000) {
            last_init_us = now;
            initialized = V20Modbus_Init();
            V20Modbus_Disarm();
            xEventGroupSetBits(handle_ThreadEventGroup, EVT_MODBUS_TASK_READY);
        }
        Actions a;
        portENTER_CRITICAL(&action_lock);
        a = actions;
        actions = (Actions){0};
        portEXIT_CRITICAL(&action_lock);
        MotorRequest req = {0};
        SafetySnapshot safety = {0};
        bool have_req = xQueuePeek(motor_requests, &req, 0) == pdTRUE;
        bool have_safety = xQueuePeek(safety_status, &safety, 0) == pdTRUE;
        bool safe = have_safety && SafetyTime_Fresh(&safety, (uint32_t)(esp_timer_get_time()/1000)) &&
            safety.commissioned && safety.relay_ok && !safety.estop_pressed &&
            safety.outputs_ok && SafetyIO_Fault() == FAULT_NONE;
        bool fresh = have_req && (TickType_t)(xTaskGetTickCount() - req.tick) < pdMS_TO_TICKS(50);
        if (a.disarm || !safe || !fresh || req.coast || !safety.stop_ok) {
            pending_arm = false;
            if (a.disarm) V20Modbus_Disarm();
        }
        if (!safety.stop_ok || req.coast) req.run = false;
        if (a.reset && safe && fresh && !req.run) {
            rejected = false;
            V20Modbus_RequestRecheck();
            (void)V20Modbus_RequestFaultAck();
        }
        if (a.arm && !a.reset && safe && fresh && safety.stop_ok && !req.coast && !rejected) {
            pending_arm = true;
            arm_epoch = a.epoch;
            arm_us = a.arm_us;
        }
        if (pending_arm) {
            if (!safe || !fresh || req.coast || !safety.stop_ok || esp_timer_get_time() - arm_us >= MODBUS_TASK_SNAPSHOT_MAX_AGE_US) {
                pending_arm = false;
                rejected = true;
                V20Modbus_Disarm();
            } else if (req.run && req.epoch == arm_epoch && req.permissive) {
                pending_arm = false;
                if (!initialized || !V20Modbus_Arm()) rejected = true;
            }
        }
        if (rejected) {
            req.run = false;
        }
        /* Recheck cancellation and current demand after any preemption. */
        MotorRequest latest = {0};
        portENTER_CRITICAL(&action_lock);
        bool cancelled = actions.disarm || actions.reset;
        portEXIT_CRITICAL(&action_lock);
        if (cancelled || xQueuePeek(motor_requests, &latest, 0) != pdTRUE ||
            latest.epoch != req.epoch || !latest.run || latest.coast) {
            req.run = false;
            if (cancelled) { pending_arm = false; V20Modbus_Disarm(); }
        }
        if (initialized) V20Modbus_Service(fresh ? &req : NULL, safe);
        V20Modbus_GetStatus(&owner_snapshot.motor);
        if (rejected) {
            owner_snapshot.motor.run_locked = true;
            owner_snapshot.motor.ok = false;
            owner_snapshot.motor.communication_ok = false;
        }
        V20Modbus_GetProofTimes(&owner_snapshot.start_proof_us, &owner_snapshot.stop_proof_us);
        if (rejected) owner_snapshot.start_proof_us = owner_snapshot.stop_proof_us = 0;
        owner_snapshot.published_us = esp_timer_get_time();
        xQueueOverwrite(snapshots, &owner_snapshot);
        xQueueOverwrite(motor_status, &owner_snapshot.motor);
        vTaskDelay(period);
    }
}
