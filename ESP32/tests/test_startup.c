/*
 * test_startup.c
 * Verifies creation order and real task readiness paths without hardware.
 * Author: R.E. van Eekelen
 */
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "../src/ApplicationTasks.c"
#include "../src/InputHandlerTask.c"
#include "../src/SensorTask.c"

static Queue queues[7];
static EventBits_t groups[4];
static unsigned queue_count, group_count, task_count, watchdog_calls;
static unsigned hardware_stage;
static bool output_ok = true, sensor_init_ok = true;
static jmp_buf task_finished;

void SafetyIO_Init(void) { assert(hardware_stage == 0); hardware_stage = 1; }
bool I2CBus_Init(void) { assert(hardware_stage == 1); hardware_stage = 2; return output_ok; }
QueueHandle_t xQueueCreate(unsigned length, unsigned size)
{
    assert(hardware_stage == 2 && queue_count < 7 && task_count == 0);
    assert(length == (queue_count == 2 ? 16u : 1u));
    Queue *q = &queues[queue_count++];
    q->size = size;
    return q;
}
EventGroupHandle_t xEventGroupCreate(void)
{
    assert(queue_count == 7 && group_count < 4 && task_count == 0);
    return &groups[group_count++];
}
BaseType_t xTaskCreatePinnedToCore(void (*task)(void*),const char *name,unsigned stack,
                                 void *argument,unsigned priority,TaskHandle_t *handle,int core)
{
    assert(queue_count == 7 && group_count == 4 && task_count < 4 && argument == NULL);
    assert(commands && sensors && motor_requests && motor_status && telemetry && safety_status && pi_heartbeat);
    assert(events && handle_ButtonEventGroup && handle_ThreadEventGroup && handle_SystemEventGroup);
    const unsigned stacks[] = {3072,4096,6144,6144}, priorities[] = {5,3,4,2};
    const int cores[] = {0,0,1,0};
    void (*tasks[])(void*) = {InputHandlerTask,SensorTask,ControlTask,PiCommunicationTask};
    const char *names[] = {"InputHandler","SensorTask","ControlTask","PiCommunication"};
    assert(stack == stacks[task_count] && priority == priorities[task_count] && core == cores[task_count]);
    assert(task == tasks[task_count] && strcmp(name,names[task_count]) == 0);
    if (handle) *handle = (void*)1;
    task_count++;
    return pdPASS;
}
void ControlTask(void *argument) { (void)argument; }
void PiCommunicationTask(void *argument) { (void)argument; }
TickType_t xTaskGetTickCount(void) { return 100; }
int esp_task_wdt_add(void *argument) { (void)argument; return ESP_OK; }
int esp_task_wdt_reset(void) { watchdog_calls++; return ESP_OK; }
void vTaskDelayUntil(TickType_t *wake,TickType_t period)
{
    (void)wake;
    assert(period == 10 || period == SENSOR_PERIOD_MS);
    longjmp(task_finished,1);
}
int xQueuePeek(QueueHandle_t q,void *data,TickType_t wait)
{
    assert(wait == 0);
    if (!q->count) return pdFALSE;
    memcpy(data,q->data,q->size);
    return pdTRUE;
}
int xQueueOverwrite(QueueHandle_t q,const void *data)
{
    memcpy(q->data,data,q->size);q->count=1;return pdTRUE;
}
EventBits_t xEventGroupSetBits(EventGroupHandle_t group,EventBits_t bits) { return *group |= bits; }
SafetySnapshot SafetyIO_Read(void) { return (SafetySnapshot){.tick_ms=100}; }
bool SafetyIO_Inhibited(void) { return true; }
bool DigitalOutputs_Set(bool enabled) { assert(!enabled); return output_ok; }
void SafetyIO_PublishButtonEvents(const SafetySnapshot *current,const SafetySnapshot *previous)
{
    (void)previous;
    SafetySnapshot published;
    assert(xQueuePeek(safety_status,&published,0));
    assert(published.tick_ms == current->tick_ms);
}
bool SensorLib_Init(void) { return sensor_init_ok; }
bool SensorLib_Read(float *value) { *value=0;return false; } /* Calibration not complete. */
void SensorLib_Details(Sample *sample) { (void)sample; }

int main(void)
{
    StartApplicationTasks();
    assert(task_count == 4 && *handle_ThreadEventGroup == 0);
    const unsigned sizes[] = {sizeof(SafetySnapshot),sizeof(PiHeartbeat),sizeof(PiCommand),
        sizeof(Sample),sizeof(MotorRequest),sizeof(MotorStatus),sizeof(Telemetry)};
    for (unsigned i=0;i<7;i++) assert(queues[i].size == sizes[i]);
    if (!setjmp(task_finished)) InputHandlerTask(NULL);
    assert((*handle_ThreadEventGroup & EVT_INPUT_TASK_READY) && watchdog_calls == 1);
    *handle_ThreadEventGroup=0;output_ok=false;
    if (!setjmp(task_finished)) InputHandlerTask(NULL);
    assert(!(*handle_ThreadEventGroup & EVT_INPUT_TASK_READY));
    if (!setjmp(task_finished)) SensorTask(NULL);
    assert(*handle_ThreadEventGroup & EVT_SENSOR_TASK_READY);
    Sample sample;assert(xQueuePeek(sensors,&sample,0) && !sample.valid);
    assert(*handle_SystemEventGroup & EVT_SENSOR_UPDATED);
    *handle_ThreadEventGroup=0;sensor_init_ok=false;
    if (!setjmp(task_finished)) SensorTask(NULL);
    assert(!(*handle_ThreadEventGroup & EVT_SENSOR_TASK_READY));
    puts("PASS queue/event creation before tasks; unchanged scheduling; successful local initialization only");
    puts("PASS readiness independent of sensor calibration and physical health; failed init remains inhibited");
    return 0;
}
