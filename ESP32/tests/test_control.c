/*
 * test_control.c
 * Executes the production control and input functions with host-only RTOS mocks.
 * Including the implementation keeps private helpers private in the firmware.
 *
 * Author: R.E. van Eekelen
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/ControlTask.c"
#include "../src/InputHandlerTask.c"
#include "FaultSnapshot.h"

static bool ethernet_up = true;
bool Ethernet_LinkUp(void) { return ethernet_up; }
static DigitalOutputStatus output_status;
bool I2CBus_Init(void) { return true; }
void DigitalOutputs_GetStatus(DigitalOutputStatus *status)
{
    if (status != NULL)
        *status = output_status;
}

/* Calibration storage is exercised by the separate NVS host suite. */
static bool calibration_enabled;
bool FanCalibration_IsValid(void) { return calibration_enabled; }
bool FanCalibration_ControlGate(bool safe) { (void)safe;return false; }
float FanCalibration_GetBaseFrequency(float v,float f) { (void)v;return calibration_enabled?20.0f:f; }
static Queue q[7];
QueueHandle_t commands = &q[0], sensors = &q[1], motor_requests = &q[2], motor_status = &q[3],
              telemetry = &q[4], safety_status = &q[5], pi_heartbeat = &q[6];
static EventBits_t button_bits, thread_bits, system_bits, stop_bits;
EventGroupHandle_t handle_ButtonEventGroup = &button_bits, handle_ThreadEventGroup = &thread_bits,
                   handle_SystemEventGroup = &system_bits, events = &stop_bits;
TaskHandle_t control_task_handle, sensor_task_handle;
static int64_t fake_us;
static unsigned watchdog_resets, arms, disarms, timer_starts;
static bool publish_heartbeat_during_read;
static bool stop_confirmed;
static TaskHandle_t notified[4];
static unsigned notification_count;
static MotorStatus drive = {
    .ready = true, .ok = true, .initialized = true, .response_received = true,
    .status_healthy = true, .communication_ok = true, .drive_fault_free = true,
    .operational_ready = true, .diagnostics_complete = true, .status_valid = true,
    .reference_valid = true, .reference_config_valid = true, .status = 0x0231,
    .stopped_command_acknowledged = true, .reference_value = 5000
};
int64_t esp_timer_get_time(void)
{
    return fake_us;
}
int esp_task_wdt_add(void *p)
{
    (void)p;
    return 0;
}
int esp_task_wdt_reset(void)
{
    watchdog_resets++;
    return 0;
}
TaskHandle_t xTaskGetCurrentTaskHandle(void)
{
    return (void *)1;
}
TickType_t xTaskGetTickCount(void)
{
    return (TickType_t)(fake_us / 1000);
}
void vTaskNotifyGiveFromISR(TaskHandle_t t, BaseType_t *w)
{
    notified[notification_count++]=t;
    (void)w;
}
uint32_t ulTaskNotifyTake(int clear, TickType_t wait)
{
    (void)clear;
    fake_us += wait * 1000;
    return 0;
}
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t t)
{
    (void)t;
    return 1024;
}
void vTaskDelay(TickType_t t)
{
    fake_us += t * 1000;
}
void vTaskDelayUntil(TickType_t *wake, TickType_t period)
{
    *wake += period;
    fake_us = (int64_t)*wake * 1000;
}
bool DigitalOutputs_Set(bool enabled, StatusIndicator indicators)
{
    (void)enabled;
    (void)indicators;
    return true;
}
EventBits_t xEventGroupSetBits(EventGroupHandle_t g, EventBits_t bits)
{
    return *g |= bits;
}
EventBits_t xEventGroupWaitBits(EventGroupHandle_t g, EventBits_t bits, int clear, int all,
                                TickType_t wait)
{
    bool match = all ? ((*g & bits) == bits) : ((*g & bits) != 0);
    if (!match)
        fake_us += (int64_t)wait * 1000;
    EventBits_t result = *g;
    if (clear && match)
        *g &= ~bits;
    return result;
}
int xQueuePeek(QueueHandle_t p, void *dst, TickType_t wait)
{
    assert(wait == 0);
    if (!p->count)
        return 0;
    if (p == pi_heartbeat && publish_heartbeat_during_read)
    {
        publish_heartbeat_during_read = false;
        fake_us += 1000;
        ((PiHeartbeat *)p->data)->tick = xTaskGetTickCount();
    }
    memcpy(dst, p->data, p->size);
    return 1;
}
int xQueueOverwrite(QueueHandle_t p, const void *src)
{
    memcpy(p->data, src, p->size);
    p->count = 1;
    return 1;
}
int xQueueReceive(QueueHandle_t p, void *dst, TickType_t wait)
{
    assert(wait == 0);
    if (!p->count)
        return 0;
    memcpy(dst, p->data, p->size);
    --p->count;
    memmove(p->data, p->data + p->size, p->count * p->size);
    return 1;
}
int gptimer_new_timer(const gptimer_config_t *c, gptimer_handle_t *t)
{
    (void)c;
    *t = (void *)1;
    return 0;
}
int gptimer_set_alarm_action(gptimer_handle_t t, const gptimer_alarm_config_t *a)
{
    (void)t;
    assert(a->alarm_count == SENSOR_PHASE_US); /* Two 5ms phases produce 100Hz control. */
    return 0;
}
int gptimer_register_event_callbacks(gptimer_handle_t t, const gptimer_event_callbacks_t *c,
                                     void *p)
{
    (void)t;
    (void)c;
    (void)p;
    return 0;
}
int gptimer_enable(gptimer_handle_t t)
{
    (void)t;
    return 0;
}
int gptimer_start(gptimer_handle_t t)
{
    (void)t;
    assert(control_task_handle);
    timer_starts++;
    return 0;
}
bool V20Modbus_Init(void)
{
    return true;
}
void V20Modbus_Disarm(void)
{
    disarms++;
}
bool V20Modbus_Arm(void)
{
    arms++;
    return true;
}
void V20Modbus_RequestRecheck(void)
{
}
bool V20Modbus_RequestFaultAck(void) { return false; }
bool V20Modbus_StartConfirmed(int64_t t) { (void)t;return drive.operation_enabled; }
bool V20Modbus_StopConfirmed(int64_t t, float hz)
{
    (void)t;
    (void)hz;
    return stop_confirmed;
}
void V20Modbus_GetStatus(MotorStatus *m)
{
    *m = drive;
}
bool SensorLib_PitotCommunicationOk(void)
{
    return true;
}
void V20Modbus_Service(const MotorRequest *r, bool ok)
{
    (void)r;
    (void)ok;
}

static SafetySnapshot healthy(void)
{
    return (SafetySnapshot){.valid = true,
                            .commissioned = true,
                            .relay_ok = true,
                            .stop_ok = true,
                            .outputs_ok = true,
                            .tick_ms = 1000};
}
static void healthy_context(ControlContext *c)
{
    *c = (ControlContext){0};
    c->v20_initialized = true;
    c->now_ms = 1000;
    c->now_tick = 1000;
    c->safety = healthy();
    c->motor = drive;
    c->motor.operation_enabled = false;
    c->status.sensor_ok = c->status.pi_ok = true;
    StateMachine_Init(&c->machine, 0);
    c->machine.state = STATE_READY;
    c->machine.entered_ms = 900;
    const RegelaarParameters parameters = {.kp=2, .ti_s=2, .tf_s=0.05f,
        .sample_s=CONTROL_CLOCK_PERIOD_S, .mode=REGELAAR_PI};
    assert(Regelaar_Init(&c->controller, &parameters));
#if CONTROL_TYPE == CONTROL_TORQUE_PI
    c->torque_model_ready = true;
    c->torque_model = (TorqueModel){.inertia_kgm2=0.3f, .slip_gain_nm_per_rad_s=1,
        .load_coeff_nm_per_rad_s2=0.001f, .pole_pairs=2,
        .nominal_air_omega_gain=0.25f, .max_command_hz=50};
#else
    c->frequency_controller_ready = true;
    c->frequency_model = (FrequencyModel){.max_hz=50, .commissioned=true,
        .identified_valid=true, .gain_mps_per_hz=0.8f, .tau_s=2, .delay_s=0.1f, .lambda_s=1};
    calibration_enabled = true;
#endif
}
static void test_start_configuration(void)
{
    ControlContext c;
    healthy_context(&c);
    c.status.mode=MODE_AUTO;
    c.motor.status=0xeb31; c.motor.operation_enabled=false;
    c.motor.watchdog_config_known=c.motor.watchdog_config_valid=false;
    const unsigned initial_arms=arms;
    /* READY has validated drive/safety readiness; no START means no arm. */
    Control_CheckSafety(&c); Control_UpdateStateMachine(&c); Control_PublishStatus(&c);
    assert(c.machine.state==STATE_READY && arms==initial_arms);
    assert(c.status.configuration.start_ready); /* Optional unknown watchdog mapping is not a failure. */
    c.safety.start_sequence=1; c.safety.start_ms=950;
    Control_ProcessCommands(&c,0); Control_CheckSafety(&c); Control_UpdateStateMachine(&c);
    assert(c.machine.state==STATE_STARTING && c.machine.fault==FAULT_NONE && arms==initial_arms+1);
    Control_UpdateController(&c); Control_PublishStatus(&c);
    MotorRequest request; xQueuePeek(motor_requests,&request,0);
    assert(request.run && request.permissive && request.output==0); /* First RUN is a zero reference. */
    c.motor.operation_enabled=true; c.motor.status=0xeb37; drive.operation_enabled=true;
    Control_CheckSafety(&c); Control_UpdateStateMachine(&c);
    assert(c.machine.state==STATE_RUNNING); drive.operation_enabled=false;
    healthy_context(&c); c.status.mode=MODE_AUTO;
#if CONTROL_TYPE == CONTROL_FREQUENCY_PI
    c.frequency_model.gain_mps_per_hz=0; c.frequency_model.tau_s=0;
    c.frequency_controller_ready=false;
#else
    c.torque_model_ready=false;
#endif
    Control_CheckSafety(&c); Control_UpdateStateMachine(&c);
    assert(c.machine.state==STATE_READY); /* Operator can still select MANUAL in READY. */
    const unsigned before=arms;
    c.safety.start_sequence=1; c.safety.start_ms=950;
    Control_ProcessCommands(&c,0); Control_CheckSafety(&c); Control_UpdateStateMachine(&c);
    assert(c.machine.state==STATE_FAULT && c.machine.fault==FAULT_CONFIGURATION && arms==before);
    assert(c.machine.fault_from==STATE_READY && c.status.first_fault.previous_state==STATE_READY);
#if CONTROL_TYPE == CONTROL_FREQUENCY_PI
    assert(!strcmp(c.status.first_fault.configuration.failure_reason,"FREQUENCY_MODEL_GAIN_INVALID"));
#else
    assert(!strcmp(c.status.first_fault.configuration.failure_reason,"TORQUE_CONTROLLER_INVALID"));
#endif
    const uint32_t sequence=c.status.first_fault.sequence;
    c.status.mode=MODE_MANUAL; c.start=false;
    Control_CheckSafety(&c); Control_UpdateStateMachine(&c); Control_PublishStatus(&c);
    xQueuePeek(motor_requests,&request,0);
    assert(c.machine.state==STATE_FAULT && !request.run && !request.permissive && arms==before);
    assert(c.status.first_fault.sequence==sequence); /* Recovery cannot overwrite the first reason. */
    healthy_context(&c); c.status.mode=MODE_MANUAL; c.motor.tick=c.now_tick;
#if CONTROL_TYPE == CONTROL_FREQUENCY_PI
    c.frequency_controller_ready=false; calibration_enabled=false;
#else
    c.torque_model_ready=false;
#endif
    c.safety.start_sequence=1; c.safety.start_ms=950;
    Control_ProcessCommands(&c,0); Control_CheckSafety(&c); Control_UpdateStateMachine(&c);
    assert(c.machine.state==STATE_STARTING && c.machine.fault==FAULT_NONE);
    healthy_context(&c); c.status.mode=MODE_AUTO; c.motor.reference_config_valid=false;
    Control_CheckSafety(&c); Control_UpdateStateMachine(&c);
    assert(c.machine.fault==FAULT_CONFIGURATION);
    assert(!strcmp(c.status.first_fault.configuration.failure_reason,"V20_REFERENCE_INVALID"));
#if CONTROL_TYPE == CONTROL_FREQUENCY_PI
    healthy_context(&c); c.status.mode=MODE_AUTO; c.frequency_model.tau_s=0;
    Control_UpdateConfiguration(&c);
    assert(!strcmp(c.status.configuration.failure_reason,"FREQUENCY_MODEL_TAU_INVALID"));
    healthy_context(&c); c.status.mode=MODE_AUTO; calibration_enabled=false;
    Control_UpdateConfiguration(&c);
    assert(!strcmp(c.status.configuration.failure_reason,"FAN_CALIBRATION_MISSING"));
    healthy_context(&c);
#endif
    /* Do not leak mock arm counts into older tests' independent expectations. */
    arms=initial_arms;
    puts("PASS EB31 AUTO/MANUAL start preflight, optional watchdog mapping, precise invalid-model reasons, latch/no restart");
}

static void test_connection_and_first_fault(void)
{
    ControlContext c;
    healthy_context(&c);
    c.machine.state = STATE_RUNNING;
    c.machine.permissive = true;
    drive.operation_enabled = true;
    drive.status = 0x0237;
    c.motor = drive;
    PiHeartbeat hb = {.connected = true, .tcp_connected = true};
    Sample sample = {.valid = true};
    fake_us = 1000000;
    /* Ten simulated minutes at 100 Hz, heartbeat every 500 ms, no HMI command. */
    for (unsigned cycle = 0; cycle < 60000; ++cycle)
    {
        fake_us += CONTROL_PERIOD_US;
        Control_BeginCycle(&c, 1);
        sample.tick = xTaskGetTickCount();
        c.safety.tick_ms = (uint32_t)(fake_us / 1000);
        if (cycle % 50 == 0) hb.tick = xTaskGetTickCount();
        xQueueOverwrite(sensors, &sample);
        xQueueOverwrite(safety_status, &c.safety);
        xQueueOverwrite(pi_heartbeat, &hb);
        Control_ReadInputs(&c);
        Control_CheckSafety(&c);
        Control_UpdateStateMachine(&c);
        Control_PublishStatus(&c);
        assert(c.machine.state == STATE_RUNNING && c.machine.permissive);
    }
    /* Actual cross-core ordering: BeginCycle's clock is older than the queue item. */
    Control_BeginCycle(&c, 1);
    const TickType_t old_tick = c.now_tick;
    publish_heartbeat_during_read = true;
    Control_ReadInputs(&c);
    assert((TickType_t)(old_tick - c.heartbeat.tick) >= pdMS_TO_TICKS(COMMAND_TIMEOUT_MS));
    assert(c.status.pi_ok);
    /* Deadline is unchanged: 1999 ms accepted, exactly 2000 ms rejected. */
    hb.tick = xTaskGetTickCount() - 1999u;
    xQueueOverwrite(pi_heartbeat, &hb); Control_ReadInputs(&c);
    assert(c.status.pi_ok);
    hb.tick--; xQueueOverwrite(pi_heartbeat, &hb); Control_ReadInputs(&c);
    assert(!c.status.pi_ok);
    c.safety.tick_ms = c.now_ms;
    c.status.sensor_ok = true;
    output_status = (DigitalOutputStatus){.initialized=true, .vfd_requested=true,
        .vfd_acknowledged=true, .confirmed_valid=true};
    Control_CheckSafety(&c); Control_UpdateStateMachine(&c);
    assert(c.machine.fault == FAULT_PI_CONNECTION && !c.machine.permissive);
    assert(c.status.first_fault.valid && c.status.first_fault.code == FAULT_PI_CONNECTION);
    assert(c.status.first_fault.previous_state == STATE_RUNNING);
    assert(c.status.first_fault.heartbeat_age_ms == COMMAND_TIMEOUT_MS);
    assert(c.status.first_fault.do1_acknowledged && c.status.first_fault.do1_requested);
    const FirstFaultSnapshot first = c.status.first_fault;
    /* Concurrent/later Modbus failures and even emergency priority must not rewrite evidence. */
    Control_Fault(&c, FAULT_MODBUS, "later_modbus");
    Control_Fault(&c, FAULT_EMERGENCY_STOP, "later_estop");
    assert(!memcmp(&first, &c.status.first_fault, sizeof(first)));
    Control_PublishStatus(&c);
    MotorRequest request;
    xQueuePeek(motor_requests, &request, 0);
    assert(!request.run && !request.permissive && request.coast && request.output == 0);
    output_status.vfd_requested = output_status.vfd_acknowledged = false;
    fake_us += 10000; Control_RecordEvent(&c);
    assert(c.status.fault_do1_off_observed_ms && !c.status.fault_off2_observed_ms);
    c.motor.off2_active = true; c.motor.status_valid = true;
    fake_us += 10000; Control_RecordEvent(&c);
    assert(c.status.fault_off2_observed_ms > c.status.fault_do1_off_observed_ms);
    char formatted[4096];
    assert(FaultSnapshot_Format(&first, formatted, sizeof(formatted)) > 0);
    assert(strstr(formatted, "first_fault_code=PI_CONNECTION") && strstr(formatted, "do1_acknowledged=1"));
    assert(FaultSnapshot_Format(&first, formatted, 16) == 0);
    FirstFaultSnapshot largest=first;
    largest.history_count=FAULT_HISTORY_SIZE;
    for(unsigned i=0;i<FAULT_HISTORY_SIZE;++i)
        largest.history[i]=(FaultEvent){.timestamp_ms=UINT32_MAX,.state=STATE_STOPPED_PREPARE,
            .statusword=UINT16_MAX,.status_valid=true,.do1_requested=true,.do1_acknowledged=true,
            .do1_valid=true,.tcp_connected=true,.heartbeat_valid=true};
    largest.motor.transaction.tx_bytes=13; largest.motor.transaction.rx_bytes=32;
    memset(largest.motor.transaction.tx_frame,255,13);
    memset(largest.motor.transaction.rx_frame,255,32);
    largest.motor.transaction.id=largest.motor.transaction.start_ms=largest.motor.transaction.deadline_ms=UINT32_MAX;
    largest.motor.errors=largest.motor.timeouts=largest.motor.max_process_data_gap_ms=UINT32_MAX;
    largest.configuration.failure_reason="FREQUENCY_MODEL_NOT_COMMISSIONED";
    assert(FaultSnapshot_Format(&largest, formatted, sizeof(formatted)) > 0);
    assert(strstr(formatted,"modbus_rx_hex=ffffffff"));

    /* Recovering heartbeat/socket never resets a fault or re-arms RUN. */
    hb.tick = xTaskGetTickCount(); hb.disconnect_sequence++;
    xQueueOverwrite(pi_heartbeat, &hb);
    c.machine.fault = FAULT_PI_CONNECTION;
    c.safety.tick_ms = c.now_ms;
    Control_ReadInputs(&c); c.status.sensor_ok = true;
    Control_CheckSafety(&c); Control_UpdateStateMachine(&c);
    assert(c.machine.state == STATE_FAULT && !c.machine.permissive);
    /* A complete disconnect/reconnect between two control reads is still latched. */
    healthy_context(&c); c.machine.state=STATE_RUNNING; c.motor=drive;
    c.previous_disconnect_sequence = hb.disconnect_sequence;
    hb.disconnect_sequence++; hb.tick=xTaskGetTickCount();
    xQueueOverwrite(pi_heartbeat, &hb); Control_ReadInputs(&c);
    c.status.sensor_ok=true; c.safety.tick_ms=c.now_ms;
    Control_CheckSafety(&c); Control_UpdateStateMachine(&c);
    assert(c.machine.fault==FAULT_PI_CONNECTION);
    assert(!strcmp(c.status.first_fault.source, "pi_tcp_disconnect"));
    /* Simultaneous faults retain the existing state-machine ordering, not Pi relabeling. */
    healthy_context(&c); c.machine.state=STATE_RUNNING; c.motor=drive;
    c.motor.communication_ok=false; c.status.pi_ok=false;
    Control_CheckSafety(&c); Control_UpdateStateMachine(&c);
    assert(c.machine.fault==FAULT_MODBUS && c.status.first_fault.code==FAULT_MODBUS);
    assert(!c.status.first_fault.heartbeat_valid && !c.status.first_fault.motor.communication_ok);
    /* A real disconnect and physical Ethernet loss also fail closed. */
    healthy_context(&c); c.machine.state=STATE_RUNNING; c.motor=drive;
    hb.connected=false; hb.tcp_connected=false; hb.disconnect_sequence=0;
    xQueueOverwrite(pi_heartbeat,&hb); Control_ReadInputs(&c);
    assert(!c.status.pi_ok);
    hb.connected=hb.tcp_connected=true; hb.tick=xTaskGetTickCount();
    xQueueOverwrite(pi_heartbeat,&hb); ethernet_up=false; Control_ReadInputs(&c);
    assert(!c.status.pi_ok); ethernet_up=true;
    /* Unsigned age remains correct across RTOS tick wrap. */
    fake_us = ((int64_t)UINT32_MAX + 50LL) * 1000;
    hb.tick=UINT32_MAX-100u; xQueueOverwrite(pi_heartbeat,&hb);
    Control_ReadInputs(&c); assert(c.status.pi_ok);
    hb.tick=xTaskGetTickCount()-2000u; xQueueOverwrite(pi_heartbeat,&hb);
    Control_ReadInputs(&c); assert(!c.status.pi_ok);
    drive.operation_enabled=false; drive.status=0x0231;
    output_status=(DigitalOutputStatus){0}; fake_us=0;
    puts("PASS 10min RUN, heartbeat race/deadline/wrap, disconnect/reconnect, first-fault before DO1, no restart");
}

int main(void)
{
    const unsigned sizes[] = {sizeof(PiCommand),   sizeof(Sample),    sizeof(MotorRequest),
                              sizeof(MotorStatus), sizeof(Telemetry), sizeof(SafetySnapshot),
                              sizeof(PiHeartbeat)};
    for (unsigned i = 0; i < 7; i++)
        q[i].size = sizes[i];
    test_start_configuration();
    test_connection_and_first_fault();
    ControlContext c;
    healthy_context(&c);
    const SystemState_t blocked[] = {STATE_INIT, STATE_WAIT, STATE_STOP, STATE_FAULT};
    for (unsigned i = 0; i < 4; i++)
    {
        c.machine.state = blocked[i];
        c.safety.start_sequence++;
        c.safety.start_ms = 950;
        Control_ProcessCommands(&c, EVT_START_BUTTON);
        assert(!c.start);
        c.machine.state = STATE_READY;
        c.machine.entered_ms = 960;
        Control_ProcessCommands(&c, EVT_START_BUTTON);
        assert(!c.start);
    }
    c.safety.start_sequence++;
    c.safety.start_ms = 960;
    Control_ProcessCommands(&c, EVT_START_BUTTON);
    assert(!c.start);
    c.safety.start_sequence++;
    c.safety.start_ms = 970;
    Control_ProcessCommands(&c, 0);
    assert(c.start); /* Queue arrives before bit. */
    Control_ProcessCommands(&c, EVT_START_BUTTON);
    assert(!c.start); /* Late bit. */
    c.safety.start_sequence++;
    c.safety.start_ms = 980;
    Control_ProcessCommands(&c, EVT_START_BUTTON);
    assert(c.start);
    Control_CheckSafety(&c);
    Control_UpdateStateMachine(&c);
    assert(c.machine.state == STATE_STARTING && arms == 1);
    Control_PublishStatus(&c);
    assert(!c.status.running && ((MotorRequest *)q[2].data)->run && c.status.output == 0);
    drive.operation_enabled = true;
    c.motor = drive;
    Control_CheckSafety(&c);
    Control_UpdateStateMachine(&c);
    assert(c.machine.state == STATE_RUNNING && arms == 1);
    assert(SafetyIO_Simulate("STOP", true));
    Control_ProcessCommands(&c, 0);
    assert(c.stop);
    Control_CheckSafety(&c);
    Control_UpdateStateMachine(&c);
    assert(c.machine.state == STATE_STOP && c.machine.permissive);
    assert(SafetyIO_Simulate("STOP", false));
    (void)SafetyIO_Stop();
    stop_confirmed = true;
    Control_ProcessCommands(&c, 0);
    Control_CheckSafety(&c);
    Control_UpdateStateMachine(&c);
    assert(c.machine.state == STATE_READY && arms == 1);
    stop_bits = STOP_REQUEST;
    Control_ProcessCommands(&c, 0);
    assert(c.stop && stop_bits == 0);
    Control_ProcessCommands(&c, EVT_STOP_BUTTON);
    assert(c.stop);
    puts("PASS fresh START, no replay in any inhibited state, independent STOP, OFF1 transition");

    healthy_context(&c);
    c.machine.state = STATE_WAIT;
    c.safety.reset_sequence = 1;
    Control_ProcessCommands(&c, EVT_RESET_BUTTON);
    assert(c.reset && !c.start);
    Control_CheckSafety(&c);
    Control_UpdateStateMachine(&c);
    Control_ProcessCommands(&c, 0);
    Control_CheckSafety(&c);
    Control_UpdateStateMachine(&c);
    assert(c.machine.state == STATE_STOPPED_PREPARE && arms == 1);
    Control_CheckSafety(&c);
    Control_UpdateStateMachine(&c);
    assert(c.machine.state == STATE_READY && arms == 1);
    assert(SafetyIO_Simulate("ESTOP", true));
    SafetyIO_Acknowledge();
    assert(SafetyIO_Fault() == FAULT_EMERGENCY_STOP);
    c.safety.estop_pressed = true;
    c.safety.reset_sequence++;
    Control_ProcessCommands(&c, EVT_RESET_BUTTON);
    Control_CheckSafety(&c);
    Control_UpdateStateMachine(&c);
    assert(c.machine.state == STATE_FAULT && c.machine.fault == FAULT_EMERGENCY_STOP);
    assert(SafetyIO_Simulate("ESTOP", false));
    SafetyIO_Acknowledge();
    healthy_context(&c);
    c.machine.state = STATE_FAULT;
    c.machine.fault = FAULT_EMERGENCY_STOP;
    Control_PublishStatus(&c);
    assert(c.status.reset_required);
    c.safety.commissioned = false;
    c.status.sensor_ok = false;
    c.motor.ready = c.motor.ok = false;
    c.safety.reset_sequence = 1;
    Control_ProcessCommands(&c, EVT_RESET_BUTTON);
    Control_CheckSafety(&c);
    Control_UpdateStateMachine(&c);
    Control_ProcessCommands(&c, 0);
    Control_CheckSafety(&c);
    Control_UpdateStateMachine(&c);
    assert(c.machine.state == STATE_WAIT && c.machine.fault == FAULT_NONE &&
           c.machine.reset_accepted && !c.machine.reset_pending &&
           !c.machine.permissive && arms == 1);
    Control_PublishStatus(&c);
    assert(!c.status.reset_required);
    puts("PASS physical RESET recovery without arm; unsafe emergency latch cannot clear");

    healthy_context(&c);
    fake_us = 1000000;
    Sample sample = {.value = 12, .valid = true, .tick = 1000};
    PiHeartbeat hb = {.connected = true, .tick = 1000, .tcp_connected = true};
    xQueueOverwrite(sensors, &sample);
    xQueueOverwrite(safety_status, &c.safety);
    xQueueOverwrite(pi_heartbeat, &hb);
    system_bits = 0;
    Control_ReadInputs(&c);
    assert(c.status.sensor_ok && c.status.pi_ok && c.sample.value == 12);
    c.now_tick = 3000;
    fake_us = 3000000;
    Control_ReadInputs(&c);
    assert(!c.status.sensor_ok && !c.status.pi_ok);
    healthy_context(&c);
    c.machine.state = STATE_RUNNING;
    c.machine.permissive = true;
    c.status.output = 42;
    c.started_us = 0;
    fake_us = CONTROL_PERIOD_US;
    Control_PublishStatus(&c);
    assert(((MotorRequest *)q[2].data)->run);
    Control_UpdateDiagnostics(&c);
    Control_PublishStatus(&c);
    MotorRequest request;
    Telemetry t;
    xQueuePeek(motor_requests, &request, 0);
    xQueuePeek(telemetry, &t, 0);
    assert(c.machine.fault == FAULT_CONTROL_OVERRUN && !request.run && !request.permissive &&
           request.coast && request.output == 0);
    assert(t.fault == FAULT_CONTROL_OVERRUN && t.overruns == 1);
    healthy_context(&c);
    c.started_us = fake_us;
    c.motor.communication_ok = false;
    Control_UpdateDiagnostics(&c);
    Control_PublishStatus(&c);
    assert(c.machine.fault == FAULT_MODBUS);
    puts("PASS unconditional freshness checks and same-cycle timing/communication fault "
         "publication");

    fake_us = 0;
    Control_Init(&c);
    output_status = (DigitalOutputStatus){
        .initialized = true,
        .vfd_requested = true,
        .vfd_acknowledged = true,
        .confirmed_valid = true,
    };
    Control_PublishStatus(&c);
    Telemetry output_diagnostic;
    xQueuePeek(telemetry, &output_diagnostic, 0);
    assert(output_diagnostic.ready_diag.do1_requested &&
           output_diagnostic.ready_diag.do1_acknowledged &&
           output_diagnostic.ready_diag.do1_ack_valid);
    output_status = (DigitalOutputStatus){0};
    thread_bits = EVT_ALL_TASKS_READY;
    Control_WaitForTasks(&c);
    assert(c.tasks_ready && thread_bits == EVT_ALL_TASKS_READY && timer_starts == 0);
    fake_us = 0;
    thread_bits = EVT_INPUT_TASK_READY;
    Control_Init(&c);
    Control_WaitForTasks(&c);
    assert(!c.tasks_ready && fake_us == 15000000 && c.machine.fault == FAULT_INITIALIZATION &&
           timer_starts == 0);
    assert(watchdog_resets >= 300);
    Telemetry startup_fault;
    xQueuePeek(telemetry, &startup_fault, 0);
    assert(startup_fault.state == STATE_FAULT && startup_fault.fault == FAULT_INITIALIZATION &&
           startup_fault.fault_from == STATE_INIT);
    xQueuePeek(motor_requests, &request, 0);
    assert(!request.run && !request.permissive && request.output == 0);
    puts("PASS bounded startup timeout, sticky ready bits, watchdog servicing and safe output");

    SafetySnapshot previous = healthy(), s = previous;
    Buttons b = {0};
    s.tick_ms = 0;
    Buttons_Update(&b, &s, 0);
    SafetyIO_PublishButtonEvents(&s, &previous);
    previous = s;
    s.tick_ms = 40;
    Buttons_Update(&b, &s, 40);
    previous = s;
    s.start_pressed = true;
    s.tick_ms = 50;
    Buttons_Update(&b, &s, 50);
    SafetyIO_PublishButtonEvents(&s, &previous);
    assert(Control_ReadButtonEvents() == 0);
    previous = s;
    s.tick_ms = 80;
    Buttons_Update(&b, &s, 80);
    SafetyIO_PublishButtonEvents(&s, &previous);
    assert(Control_ReadButtonEvents() == EVT_START_BUTTON);
    previous = s;
    s.tick_ms = 120;
    Buttons_Update(&b, &s, 120);
    SafetyIO_PublishButtonEvents(&s, &previous);
    assert(Control_ReadButtonEvents() == 0);
    previous = s;
    s.reset_pressed = true;
    s.tick_ms = 130;
    Buttons_Update(&b, &s, 130);
    previous = s;
    s.tick_ms = 160;
    Buttons_Update(&b, &s, 160);
    SafetyIO_PublishButtonEvents(&s, &previous);
    assert(Control_ReadButtonEvents() == 0);
    previous = s;
    s.reset_pressed = false;
    s.tick_ms = 170;
    Buttons_Update(&b, &s, 170);
    previous = s;
    s.tick_ms = 200;
    Buttons_Update(&b, &s, 200);
    SafetyIO_PublishButtonEvents(&s, &previous);
    assert(Control_ReadButtonEvents() == EVT_RESET_BUTTON);
    previous = s;
    s.stop_ok = false;
    s.tick_ms = 210;
    SafetyIO_PublishButtonEvents(&s, &previous);
    assert(Control_ReadButtonEvents() == 0);
    previous = s;
    s.tick_ms = 240;
    SafetyIO_PublishButtonEvents(&s, &previous);
    assert(Control_ReadButtonEvents() == EVT_STOP_BUTTON);
    previous = s;
    s.tick_ms = 300;
    SafetyIO_PublishButtonEvents(&s, &previous);
    assert(Control_ReadButtonEvents() == 0);
    puts("PASS debounced START/RESET actions and one-shot STOP notification");
    ControlTiming timing = {0};
    assert(ControlTiming_Tick(&timing, CONTROL_PERIOD_US, 1) == FAULT_NONE);
    assert(ControlTiming_Tick(&timing, 2 * CONTROL_PERIOD_US, 1) == FAULT_NONE);
    assert(ControlTiming_Tick(&timing, 3 * CONTROL_PERIOD_US + 50, 1) == FAULT_NONE &&
           timing.max_jitter_us == 50);
    assert(ControlTiming_Tick(&timing, 6 * CONTROL_PERIOD_US + 50, 3) == FAULT_CONTROL_OVERRUN &&
           timing.missed_deadlines == 2 && timing.fault_reason == FAULT_CONTROL_OVERRUN &&
           timing.fault_missed_ticks == 2);
    assert(ControlTiming_Tick(&timing, 6 * CONTROL_PERIOD_US + 50 +
                                  CONTROL_CLOCK_TIMEOUT_MS * 1000u, 0) == FAULT_CLOCK_TIMEOUT &&
           timing.missed_deadlines == 2 + CONTROL_CLOCK_TIMEOUT_MS / (CONTROL_PERIOD_US / 1000) &&
           timing.fault_reason == FAULT_CONTROL_OVERRUN);
    assert(ControlTiming_Execution(&timing, CONTROL_PERIOD_US - 1, 999));
    assert(!ControlTiming_Execution(&timing, CONTROL_PERIOD_US, 999));
    ControlTiming execution_fault = {0};
    assert(!ControlTiming_Execution(&execution_fault, CONTROL_PERIOD_US, 999) &&
           execution_fault.fault_reason == FAULT_CONTROL_OVERRUN &&
           execution_fault.fault_execution_us == CONTROL_PERIOD_US &&
           execution_fault.fault_service_us == 999);
    assert(!ControlTiming_Execution(&timing, 4999, V20_SERVICE_BUDGET_US) && timing.overruns == 2);
    ControlTiming tolerated_timing = {0};
    assert(ControlTiming_Tick(&tolerated_timing, CONTROL_PERIOD_US, 1) == FAULT_NONE);
    assert(ControlTiming_Tick(&tolerated_timing, 3 * CONTROL_PERIOD_US, 1) == FAULT_NONE &&
           tolerated_timing.missed_deadlines == 1 &&
           tolerated_timing.consecutive_missed_ticks == 1);
    assert(ControlTiming_Tick(&tolerated_timing, 4 * CONTROL_PERIOD_US, 1) == FAULT_NONE &&
           tolerated_timing.consecutive_missed_ticks == 0);
    assert(ControlTiming_Tick(&tolerated_timing, 5 * CONTROL_PERIOD_US, 1) == FAULT_NONE &&
           tolerated_timing.consecutive_missed_ticks == 0);
    assert(ControlTiming_Tick(&tolerated_timing, 7 * CONTROL_PERIOD_US, 1) == FAULT_NONE &&
           tolerated_timing.consecutive_missed_ticks == 1);
    assert(ControlTiming_Tick(&tolerated_timing, 9 * CONTROL_PERIOD_US, 1) == FAULT_CONTROL_OVERRUN &&
           tolerated_timing.consecutive_missed_ticks == 2);
    ControlTiming timeout_fault = {0};
    assert(ControlTiming_Tick(&timeout_fault, CONTROL_PERIOD_US, 1) == FAULT_NONE);
    assert(ControlTiming_Tick(&timeout_fault, CONTROL_PERIOD_US +
                                  CONTROL_CLOCK_TIMEOUT_MS * 1000u, 0) == FAULT_CLOCK_TIMEOUT &&
           timeout_fault.fault_reason == FAULT_CLOCK_TIMEOUT &&
           timeout_fault.fault_missed_ticks == CONTROL_CLOCK_TIMEOUT_MS / (CONTROL_PERIOD_US / 1000));
    assert(ControlTiming_Execution(&tolerated_timing, CONTROL_PERIOD_US - 1, 999));
    assert(!ControlTiming_Execution(&tolerated_timing, CONTROL_PERIOD_US, 999));
    healthy_context(&c);
    c.machine.epoch = 42;
    Control_PublishStatus(&c);
    xQueuePeek(motor_requests, &request, 0);
    assert(request.epoch == 42);
    puts("PASS timing boundaries, missed deadlines and state epoch publication");
    healthy_context(&c);
    PiCommand mode = {.type=CMD_MODE,.values={MODE_MANUAL}};
    xQueueOverwrite(commands,&mode);
    Control_ProcessCommands(&c,0);
    assert(c.status.mode==MODE_MANUAL && !c.start && arms==1 && c.status.output==0);
    c.machine.state=STATE_RUNNING;
    mode.values[0]=MODE_AUTO;
    xQueueOverwrite(commands,&mode);
    Control_ProcessCommands(&c,0);
    assert(c.status.mode==MODE_MANUAL);
    c.machine.state=STATE_STOP;
    xQueueOverwrite(commands,&mode);Control_ProcessCommands(&c,0);
    assert(c.status.mode==MODE_MANUAL && arms==1);
    c.machine.state=STATE_RUNNING;
    c.motor.actual_hz=10; c.motor.tick=c.now_tick;
    c.status.manual_hz=50;
    c.sample.value=999; /* Would produce a PI error; MANUAL must ignore it. */
    const Regelaar before=c.controller;
    Control_UpdateController(&c);
    assert(c.frequency_running && c.status.applied_hz>=10 && c.status.applied_hz<10.001f);
    assert(memcmp(&before,&c.controller,sizeof(before))==0);
    float previous_hz=c.status.applied_hz;
    const int active_steps = (int)(c.frequency_curve.duration / CONTROL_CLOCK_PERIOD_S) - 2;
    assert(active_steps > 100);
    for(int n=0;n<active_steps;n++) {
        if(n==100) c.status.manual_hz=0;
        Control_UpdateController(&c);
        assert(fabsf(c.status.applied_hz-previous_hz)<=
               MANUAL_MAX_SLEW_HZ_PER_S*CONTROL_CLOCK_PERIOD_S+0.00001f);
        previous_hz=c.status.applied_hz;
        assert(c.frequency_curve.target==50); /* Latest request deferred. */
    }
    StateMachine_Fault(&c.machine,FAULT_EMERGENCY_STOP,c.now_ms);
    Control_UpdateController(&c); Control_PublishStatus(&c);
    assert(!c.frequency_running && c.status.output==0 && !((MotorRequest*)q[2].data)->run);
    healthy_context(&c); c.status.mode=MODE_MANUAL;
    c.safety.start_sequence=1;c.safety.start_ms=950;
    c.motor.tick=c.now_tick-MOTOR_STATUS_TIMEOUT_MS;
    Control_ProcessCommands(&c,0);Control_CheckSafety(&c);Control_UpdateStateMachine(&c);
    assert(c.machine.state==STATE_FAULT && arms==1);
    healthy_context(&c);c.machine.state=STATE_RUNNING;c.sample.value=0;c.status.setpoint=10;
    Control_UpdateController(&c);
    assert(c.curve_running && !c.frequency_running && c.controller.previous_valid);
    puts("PASS manual quintic, no torque PID in MANUAL, AUTO torque PID only, deferred target, stale feedback inhibits arm, immediate fault bypass");
    control_task_handle=(void*)1; sensor_task_handle=(void*)2;
    for(unsigned n=0;n<4;n++) (void)ControlClockInterrupt(NULL,NULL,NULL);
    assert(notified[0]==sensor_task_handle && notified[1]==control_task_handle &&
           notified[2]==sensor_task_handle && notified[3]==control_task_handle);
    puts("PASS alternating 5ms sensor/control phases preserve 10ms control period");
    puts("All focused control regression checks passed (mocked hardware/RTOS).");
    healthy_context(&c);
    c.status.mode=MODE_AUTO;
    c.safety.start_sequence=1;c.safety.start_ms=950;
#if CONTROL_TYPE == CONTROL_TORQUE_PI
    c.torque_model_ready=false;
#else
    c.frequency_controller_ready=false;
#endif
    const unsigned arms_before_inhibited_start=arms;
    Control_ProcessCommands(&c,0);Control_CheckSafety(&c);Control_UpdateStateMachine(&c);
    assert(c.machine.state==STATE_FAULT && arms==arms_before_inhibited_start);
#if CONTROL_TYPE == CONTROL_FREQUENCY_PI
    healthy_context(&c);c.status.mode=MODE_AUTO;calibration_enabled=false;
    c.safety.start_sequence=1;c.safety.start_ms=950;
    Control_ProcessCommands(&c,0);Control_CheckSafety(&c);Control_UpdateStateMachine(&c);
    assert(c.machine.state==STATE_FAULT && arms==arms_before_inhibited_start);
#endif
    puts("PASS invalid selected model and unavailable frequency calibration inhibit AUTO start without arming");
    healthy_context(&c);c.machine.state=STATE_RUNNING;c.sample.value=-1;
    Control_UpdateController(&c);
    assert(c.machine.state==STATE_FAULT && c.status.output==0);
    puts("PASS invalid negative airspeed cannot enter either automatic plant");
    /* Released but still-latched safety must recover through physical RESET,
     * without the test manually calling SafetyIO_Acknowledge first. */
    fake_us = 1000000;
    (void)SafetyIO_Simulate("RELAY", true);
    (void)SafetyIO_Simulate("STOP", false);
    (void)SafetyIO_Simulate("ESTOP", false);
    SafetyIO_Acknowledge();
    healthy_context(&c); c.machine.state=STATE_FAULT; c.machine.fault=FAULT_EMERGENCY_STOP;
    unsigned arms_before=arms;
    assert(SafetyIO_Simulate("ESTOP", true));
    assert(SafetyIO_Simulate("ESTOP", false));
    assert(SafetyIO_Fault()==FAULT_EMERGENCY_STOP);
    c.safety=SafetyIO_Read(); c.reset=false;
    Control_CheckSafety(&c); Control_UpdateStateMachine(&c);
    assert(c.machine.state==STATE_FAULT && SafetyIO_Fault()==FAULT_EMERGENCY_STOP);
    c.reset=true;
    Control_CheckSafety(&c);
    assert(SafetyIO_Fault()==FAULT_NONE);
    Control_UpdateStateMachine(&c);
    assert(c.machine.state==STATE_WAIT && arms==arms_before);
    c.reset=false; Control_CheckSafety(&c); Control_UpdateStateMachine(&c);
    assert(c.machine.state==STATE_STOPPED_PREPARE && arms==arms_before);
    Control_CheckSafety(&c); Control_UpdateStateMachine(&c);
    assert(c.machine.state==STATE_READY && arms==arms_before);
    (void)SafetyIO_Simulate("ESTOP", true); c.safety=SafetyIO_Read(); c.reset=true;
    Control_CheckSafety(&c); Control_UpdateStateMachine(&c);
    assert(SafetyIO_Fault()==FAULT_EMERGENCY_STOP && c.machine.state==STATE_FAULT && arms==arms_before);
    (void)SafetyIO_Simulate("ESTOP", false); SafetyIO_Acknowledge();
    /* Diagnostic milliseconds are independent of the RTOS tick value. */
    healthy_context(&c); c.now_tick=42; c.now_ms=1000; c.safety.tick_ms=950;
    Control_PublishStatus(&c); assert(c.status.ready_diag.safety_inputs_fresh);
    c.safety.tick_ms=900; Control_PublishStatus(&c);
    assert(!c.status.ready_diag.safety_inputs_fresh);
    puts("PASS released safety latch RESET integration, active emergency rejection, no automatic START, millisecond freshness");
    healthy_context(&c); c.machine.state=STATE_FAULT; c.machine.fault=FAULT_INITIALIZATION;
    c.tasks_ready=false; thread_bits=EVT_ALL_TASKS_READY;
    Control_HoldInitializationFault(&c);
    assert(c.tasks_ready && c.machine.state==STATE_INIT && !c.machine.reset_accepted);
    Control_PublishStatus(&c); assert(!((MotorRequest*)q[2].data)->run);
    puts("PASS recovered task barrier resumes INIT without reset acceptance or RUN");
    return 0;
}
