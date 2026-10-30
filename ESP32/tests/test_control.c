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

static Queue q[7];
QueueHandle_t commands = &q[0], sensors = &q[1], motor_requests = &q[2], motor_status = &q[3],
              telemetry = &q[4], safety_status = &q[5], pi_heartbeat = &q[6];
static EventBits_t button_bits, thread_bits, system_bits, stop_bits;
EventGroupHandle_t handle_ButtonEventGroup = &button_bits, handle_ThreadEventGroup = &thread_bits,
                   handle_SystemEventGroup = &system_bits, events = &stop_bits;
TaskHandle_t control_task_handle, sensor_task_handle;
static int64_t fake_us;
static unsigned watchdog_resets, arms, disarms, timer_starts;
static bool stop_confirmed;
static TaskHandle_t notified[4];
static unsigned notification_count;
static MotorStatus drive = {.ready = true, .ok = true, .status = 0x0201};
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
bool DigitalOutputs_Set(bool enabled)
{
    (void)enabled;
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
    assert(a->alarm_count == 2500); /* Two 2.5ms phases produce 200Hz control. */
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
    c->status.sensor_ok = c->status.pi_ok = true;
    StateMachine_Init(&c->machine, 0);
    c->machine.state = STATE_READY;
    c->machine.entered_ms = 900;
    assert(Regelaar_INIT(&c->controller, CONTROL_CLOCK_PERIOD_S));
}
int main(void)
{
    const unsigned sizes[] = {sizeof(PiCommand),   sizeof(Sample),    sizeof(MotorRequest),
                              sizeof(MotorStatus), sizeof(Telemetry), sizeof(SafetySnapshot),
                              sizeof(PiHeartbeat)};
    for (unsigned i = 0; i < 7; i++)
        q[i].size = sizes[i];
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
    puts("PASS physical RESET recovery without arm; unsafe emergency latch cannot clear");

    healthy_context(&c);
    Sample sample = {.value = 12, .valid = true, .tick = 1000};
    PiHeartbeat hb = {.connected = true, .tick = 1000};
    xQueueOverwrite(sensors, &sample);
    xQueueOverwrite(safety_status, &c.safety);
    xQueueOverwrite(pi_heartbeat, &hb);
    system_bits = 0;
    Control_ReadInputs(&c);
    assert(c.status.sensor_ok && c.status.pi_ok && c.sample.value == 12);
    c.now_tick = 3000;
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
    c.motor.ok = false;
    Control_UpdateDiagnostics(&c);
    Control_PublishStatus(&c);
    assert(c.machine.fault == FAULT_MODBUS);
    puts("PASS unconditional freshness checks and same-cycle timing/communication fault "
         "publication");

    fake_us = 0;
    Control_Init(&c);
    thread_bits = EVT_ALL_TASKS_READY;
    Control_WaitForTasks(&c);
    assert(c.tasks_ready && thread_bits == EVT_ALL_TASKS_READY && timer_starts == 0);
    fake_us = 0;
    thread_bits = EVT_INPUT_TASK_READY;
    Control_Init(&c);
    Control_WaitForTasks(&c);
    assert(!c.tasks_ready && fake_us == 15000000 && c.machine.fault == FAULT_INTERNAL &&
           timer_starts == 0);
    assert(watchdog_resets >= 300);
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
    assert(ControlTiming_Tick(&timing, 5000, 1) == FAULT_NONE);
    assert(ControlTiming_Tick(&timing, 10000, 1) == FAULT_NONE);
    assert(ControlTiming_Tick(&timing, 15050, 1) == FAULT_NONE && timing.max_jitter_us == 50);
    assert(ControlTiming_Tick(&timing, 30050, 3) == FAULT_CONTROL_OVERRUN &&
           timing.missed_deadlines == 2);
    assert(ControlTiming_Tick(&timing, 80050, 0) == FAULT_CLOCK_TIMEOUT &&
           timing.missed_deadlines == 12);
    assert(ControlTiming_Execution(&timing, 4999, 999));
    assert(!ControlTiming_Execution(&timing, 5000, 999));
    assert(!ControlTiming_Execution(&timing, 4999, 1000) && timing.overruns == 2);
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
    for(int n=0;n<3000;n++) {
        if(n==100) c.status.manual_hz=0;
        Control_UpdateController(&c);
        assert(fabsf(c.status.applied_hz-previous_hz)<=2.0f*CONTROL_CLOCK_PERIOD_S+0.00001f);
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
    assert(c.curve_running && !c.frequency_running && c.controller.motorHist.gevuld);
    puts("PASS manual quintic, no PI in MANUAL, AUTO PI retained, deferred target, stale feedback inhibits arm, immediate fault bypass");
    control_task_handle=(void*)1; sensor_task_handle=(void*)2;
    for(unsigned n=0;n<4;n++) (void)ControlClockInterrupt(NULL,NULL,NULL);
    assert(notified[0]==sensor_task_handle && notified[1]==control_task_handle &&
           notified[2]==sensor_task_handle && notified[3]==control_task_handle);
    puts("PASS alternating 2.5ms sensor/control phases preserve 5ms control period");
    puts("All focused control regression checks passed (mocked hardware/RTOS).");
    return 0;
}
