/* Exercise the production serial diagnostics with bounded host log capture. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/task.h"

static unsigned summaries, snapshots, histories, tails, regular;
static void capture(const char *tag, const char *fmt, ...)
{
    char text[1024];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    assert(n >= 0 && (size_t)n < sizeof(text));
    if (!strcmp(tag, "FirstFault"))
    {
        assert(n <= 192);
        if (strstr(text, "fault=MODBUS operation=WRITE_CONTROL"))
            ++summaries;
        if (strstr(text, "FIRST_FAULT sequence="))
            ++snapshots;
        if (strstr(text, "event7="))
            ++histories;
        if (strstr(text, "modbus_rx_hex=0102"))
            ++tails;
    }
    if (strstr(text, "status_indicator="))
        ++regular;
}
#undef ESP_LOGI
#undef ESP_LOGW
#undef ESP_LOGE
#define ESP_LOGI capture
#define ESP_LOGW capture
#define ESP_LOGE capture
#define CONFIG_ESP_MAIN_TASK_STACK_SIZE 3584
#define IPSTR "%u"
#define IP2STR(ip) ((ip)->addr)
enum { eRunning, eReady, eBlocked, eSuspended, eDeleted };
static int eTaskGetState(TaskHandle_t task) { (void)task; return eBlocked; }
#include "../src/main.c"

static Queue state_queue;
static TickType_t now;
QueueHandle_t telemetry = &state_queue, pitot_samples, pressure_taps, motor_requests, motor_status, safety_status, pi_heartbeat, commands;
EventGroupHandle_t handle_ThreadEventGroup;
TaskHandle_t control_task_handle, sensor_task_handle, input_task_handle, pi_task_handle, heartbeat_task_handle;
volatile bool ota_task_alive, ota_valid_task_alive;
int esp_reset_reason(void) { return 0; }
uint32_t esp_get_free_heap_size(void) { return 0; }
TickType_t xTaskGetTickCount(void) { return now; }
int64_t esp_timer_get_time(void) { return (int64_t)now * 1000; }
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t task) { (void)task; return 0; }
void StartApplicationTasks(void) {}
void vTaskDelay(TickType_t t) { now += t; }
bool Ethernet_LinkUp(void) { return true; }
bool Ethernet_GetIpInfo(esp_netif_ip_info_t *info) { (void)info; return false; }
bool I2CBus_GetDiagnostics(uint8_t address, I2CBusDiagnostics *out) {
    memset(out,0,sizeof(*out));
    if (address == 0x20) out->results[I2C_RESULT_TIMEOUT]=1;
    return true;
}
void DigitalOutputs_GetStatus(DigitalOutputStatus *outputs) { memset(outputs, 0, sizeof(*outputs)); }
bool SafetyIO_InterpretLevel(int raw, int active, bool *result) { *result = raw == active; return true; }
const char *V20Modbus_ErrorName(V20CommError error) { (void)error; return "TIMEOUT"; }
const char *V20Modbus_OperationName(V20Operation op) { return op == V20_OP_WRITE_CONTROL ? "WRITE_CONTROL" : "NONE"; }
int xQueuePeek(QueueHandle_t q, void *dst, TickType_t wait)
{
    (void)wait;
    assert(q == telemetry);
    memcpy(dst, q->data, sizeof(Telemetry));
    return pdTRUE;
}

int main(void)
{
    Telemetry t = {0};
    FirstFaultSnapshot *f = &t.first_fault;
    f->source = "control";
    f->previous_state = STATE_RUNNING;
    f->new_state = STATE_FAULT;
    f->code = FAULT_MODBUS;
    f->history_count = FAULT_HISTORY_SIZE;
    f->motor.transaction.type = V20_OP_WRITE_CONTROL;
    f->motor.transaction.result = V20_COMM_TIMEOUT;
    f->motor.transaction.expected_rx_bytes = 8;
    f->motor.transaction.rx_bytes = 2;
    f->motor.transaction.rx_frame[0] = 1;
    f->motor.transaction.rx_frame[1] = 2;
    for (unsigned cycle = 0; cycle < 17280; ++cycle)
    {
        now += 5000;
        if (cycle % 12 == 1)
        {
            ++f->sequence;
            f->timestamp_ms = now;
            f->valid = true;
        }
        t.state = cycle % 12 ? STATE_FAULT : STATE_WAIT;
        t.fault = cycle % 12 ? FAULT_MODBUS : FAULT_NONE;
        ++t.transitions;
        memcpy(state_queue.data, &t, sizeof(t));
        LogApplicationStatus();
    }
    assert(regular == 17280 && summaries == 1440 && snapshots == summaries);
    assert(histories == summaries && tails == summaries);
    puts("PASS 24 simulated hours of periodic diagnostics and 1440 full fault reports");
    return 0;
}
