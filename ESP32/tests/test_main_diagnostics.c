/* Verify that continuous stack monitoring remains quiet unless actionable. */
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "TaskData.h"
static unsigned info_count, warning_count;
static char last_log[256];
static void capture(bool warning, const char *tag, const char *format, ...)
{
    (void)tag;
    va_list args; va_start(args,format);
    int n=vsnprintf(last_log,sizeof(last_log),format,args); va_end(args);
    assert(n>=0 && (size_t)n<sizeof(last_log));
    if(warning)warning_count++;else info_count++;
}
#undef ESP_LOGI
#undef ESP_LOGW
#define ESP_LOGI(tag, ...) capture(false,tag,__VA_ARGS__)
#define ESP_LOGW(tag, ...) capture(true,tag,__VA_ARGS__)
#include "../src/main.c"
TaskHandle_t input_task_handle=(void*)1,sensor_task_handle=(void*)2,control_task_handle=(void*)3;
TaskHandle_t pi_task_handle=(void*)4,heartbeat_task_handle=(void*)5,fault_diagnostics_task_handle=(void*)6;
static unsigned available[7]={2048,2048,2048,2048,2048,2048,2048};
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t task) { return available[(uintptr_t)task-1u]; }
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return (void*)7; }
uint32_t esp_get_free_heap_size(void) { return 200000; }
int esp_reset_reason(void) { return 0; }
bool heap_caps_check_integrity_all(bool print_errors) { assert(print_errors);return true; }
void StartApplicationTasks(void) {}
void vTaskDelay(TickType_t ticks) { (void)ticks; }
int main(void)
{
    for(unsigned i=0;i<17280;i++)MonitorTaskStacks();
    assert(info_count==1 && warning_count==0); /* 24 hours, one initial report. */
    available[0]=400;MonitorTaskStacks();
    assert(warning_count==1 && strstr(last_log,"stack_low"));
    for(unsigned i=0;i<10000;i++)MonitorTaskStacks();
    assert(warning_count==1);
    available[0]=350;MonitorTaskStacks();assert(warning_count==1);
    available[0]=200;MonitorTaskStacks();assert(warning_count==2);
    available[1]=400;MonitorTaskStacks();assert(warning_count==3);
    puts("PASS 24h quiet healthy monitoring; new/worsening low-stack warnings without repeated spam");
    return 0;
}
