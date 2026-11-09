/* Real host threads verify production sensor/output arbitration. No hardware timing claims. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include "AppConfig.h"
#include "I2CBus.h"
#include "DigitalOutputs.h"
#include "driver/i2c.h"
#include "freertos/semphr.h"
static CRITICAL_SECTION critical;
static SemaphoreHandle_t bus_mutex;
static HANDLE sensor_entered, release_sensor, output_waiting;
static bool pause_sensor, fail_latch;
static uint8_t latch;
static LONG sensor_calls, output_calls;
void Host_EnterCritical(portMUX_TYPE *lock) {(void)lock;EnterCriticalSection(&critical);}
void Host_ExitCritical(portMUX_TYPE *lock) {(void)lock;LeaveCriticalSection(&critical);}
int64_t esp_timer_get_time(void) {return (int64_t)GetTickCount64()*1000;}
SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    HANDLE m=CreateMutex(NULL,FALSE,NULL);if(!bus_mutex)bus_mutex=m;return m;
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t m,TickType_t wait) {
    if(m==bus_mutex && wait && output_waiting) SetEvent(output_waiting);
    return WaitForSingleObject(m,wait)==WAIT_OBJECT_0 ? pdTRUE:pdFALSE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t m) {return ReleaseMutex(m)?pdTRUE:pdFALSE;}
bool SafetyIO_Inhibited(void) {return false;}
esp_err_t i2c_param_config(int p,const i2c_config_t *c) {(void)p;assert(c->master.clk_speed==SENSOR_I2C_HZ);return ESP_OK;}
esp_err_t i2c_driver_install(int p,int m,size_t rx,size_t tx,int flags) {
    (void)p;(void)m;(void)rx;(void)tx;(void)flags;return ESP_OK;
}
esp_err_t i2c_master_write_to_device(int p,uint8_t address,const uint8_t *data,size_t size,TickType_t wait) {
    (void)p;assert(address==0x20 && size==2 && wait<=I2C_TIMEOUT_MS);
    InterlockedIncrement(&output_calls);if(data[0]==1) latch=data[1];return ESP_OK;
}
esp_err_t i2c_master_write_read_device(int p,uint8_t address,const uint8_t *tx,size_t tn,uint8_t *rx,size_t rn,TickType_t wait) {
    (void)p;assert(address==0x20 && *tx==1 && tn==1 && rn==1 && wait<=I2C_TIMEOUT_MS);
    InterlockedIncrement(&output_calls);*rx=latch;return fail_latch ? ESP_FAIL:ESP_OK;
}
esp_err_t i2c_master_read_from_device(int p,uint8_t address,uint8_t *rx,size_t size,TickType_t wait) {
    (void)p;(void)rx;assert(address==0x28 && size==4 && wait<=I2C_SENSOR_TIMEOUT_MS);
    InterlockedIncrement(&sensor_calls);
    if(pause_sensor) {
        SetEvent(sensor_entered);assert(WaitForSingleObject(release_sensor,5000)==WAIT_OBJECT_0);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}
static DWORD WINAPI sensor_thread(LPVOID ignored) {
    (void)ignored;uint8_t data[4];assert(!I2CBus_TryRead(0x28,data,4));return 0;
}
static DWORD WINAPI output_thread(LPVOID ignored) {
    (void)ignored;assert(DigitalOutputs_Set(true,(StatusIndicator){0}));return 0;
}
int main(void) {
    InitializeCriticalSection(&critical);assert(I2CBus_Init());
    assert(DigitalOutputs_Set(true,(StatusIndicator){0}));
    sensor_entered=CreateEvent(NULL,TRUE,FALSE,NULL);
    release_sensor=CreateEvent(NULL,TRUE,FALSE,NULL);
    output_waiting=CreateEvent(NULL,TRUE,FALSE,NULL);
    pause_sensor=true;
    HANDLE sensor=CreateThread(NULL,0,sensor_thread,NULL,0,NULL);
    assert(WaitForSingleObject(sensor_entered,5000)==WAIT_OBJECT_0);
    HANDLE output=CreateThread(NULL,0,output_thread,NULL,0,NULL);
    assert(WaitForSingleObject(output_waiting,5000)==WAIT_OBJECT_0);
    uint8_t data[4];assert(!I2CBus_TryRead(0x28,data,4));assert(sensor_calls==1);
    SetEvent(release_sensor);
    assert(WaitForSingleObject(sensor,5000)==WAIT_OBJECT_0);
    assert(WaitForSingleObject(output,5000)==WAIT_OBJECT_0);
    pause_sensor=false;assert(I2CBus_TryRead(0x28,data,4));
    fail_latch=true;assert(!DigitalOutputs_Set(true,(StatusIndicator){0}));
    DigitalOutputStatus state;DigitalOutputs_GetStatus(&state);
    assert(!state.confirmed_valid && !state.vfd_acknowledged);
    /* STOP/FAULT request deasserts DO1 immediately, despite the preceding read fault. */
    assert(DigitalOutputs_Set(false,(StatusIndicator){0}));
    fail_latch=false;assert(DigitalOutputs_Set(false,(StatusIndicator){0}));
    DigitalOutputs_GetStatus(&state);assert(!state.vfd_requested && !state.vfd_acknowledged);
    I2CBusDiagnostics d;assert(I2CBus_GetDiagnostics(0x20,&d));
    assert(d.results[I2C_RESULT_NACK]==1 && d.retries==0);
    assert(I2CBus_GetDiagnostics(0x28,&d));
    assert(d.results[I2C_RESULT_BUSY]==1 && d.results[I2C_RESULT_TIMEOUT]==1);
    puts("PASS real sensor/DO mutex contention, no busy sensor wire access, NACK fail closed, STOP and recovery remain off");
    return 0;
}
