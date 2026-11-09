/* Fault injection against production bus arbitration/diagnostics. */
#include <assert.h>
#include <stdio.h>
#include "AppConfig.h"
#include "I2CBus.h"
#include "driver/i2c.h"
#include "freertos/semphr.h"
static int64_t now;
static bool busy;
static int calls, first_error, repeat_error;
static uint32_t duration=100;
static bool wire_model;
static uint32_t acquisition_us;
int64_t esp_timer_get_time(void) { return now; }
SemaphoreHandle_t xSemaphoreCreateMutex(void) {return (void *)1;}
BaseType_t xSemaphoreTake(SemaphoreHandle_t m, TickType_t wait) {
    (void)m; if (!busy) now += acquisition_us; if (busy) now += wait*1000; return busy ? pdFALSE : pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t m) {(void)m; return pdTRUE;}
bool DigitalOutputs_Init(void) {return true;}
esp_err_t i2c_param_config(int p,const i2c_config_t *c) {(void)p;assert(c->master.clk_speed==SENSOR_I2C_HZ);return ESP_OK;}
esp_err_t i2c_driver_install(int p,int m,size_t r,size_t t,int f) {
    (void)p;(void)m;(void)r;(void)t;(void)f;return ESP_OK;
}
static esp_err_t result(TickType_t wait) {
    assert(wait>0 && wait<=I2C_TIMEOUT_MS);
    now+=duration; return ++calls==1 ? first_error : repeat_error;
}
esp_err_t i2c_master_read_from_device(int p,uint8_t a,uint8_t *d,size_t n,TickType_t w) {
    (void)p;(void)a;(void)d; if(wire_model) duration=(uint32_t)(((n+1)*9*1000000+SENSOR_I2C_HZ-1)/SENSOR_I2C_HZ);return result(w);
}
esp_err_t i2c_master_write_to_device(int p,uint8_t a,const uint8_t *d,size_t n,TickType_t w) {
    (void)p;(void)a;(void)d; if(wire_model) duration=(uint32_t)(((n+1)*9*1000000+SENSOR_I2C_HZ-1)/SENSOR_I2C_HZ);return result(w);
}
esp_err_t i2c_master_write_read_device(int p,uint8_t a,const uint8_t *tx,size_t tn,uint8_t *rx,size_t rn,TickType_t w) {
    (void)p;(void)a; assert(tx && tn==1 && *tx==1 && rx && rn==1);
    if(wire_model) duration=36*1000000/SENSOR_I2C_HZ;
    return result(w);
}
static void inject(int first,int repeat,uint32_t us) {
    calls=0;first_error=first;repeat_error=repeat;duration=us;
}
int main(void) {
    uint8_t data[8]={0};I2CBusDiagnostics d;
    assert(!I2CBus_TryRead(0x28,data,8));
    assert(I2CBus_GetDiagnostics(0x28,&d) && d.results[I2C_RESULT_DRIVER]==1);
    assert(I2CBus_Init());
    busy=true; assert(!I2CBus_TryRead(0x28,data,8));assert(calls==0);
    assert(I2CBus_GetDiagnostics(0x28,&d) && d.results[I2C_RESULT_BUSY]==1 && d.last_us==0);
    assert(!I2CBus_Write(0x20,data,2));
    assert(I2CBus_GetDiagnostics(0x20,&d) && d.max_call_us==10000 && d.last_result==I2C_RESULT_BUSY);
    busy=false;inject(ESP_FAIL,ESP_OK,100);
    assert(I2CBus_TryRead(0x28,data,8) && calls==2);
    assert(I2CBus_GetDiagnostics(0x28,&d) && d.retries==1 && d.recovered==1 && d.last_us==200 && d.last_failure==I2C_RESULT_NACK);
    inject(ESP_FAIL,ESP_FAIL,100);assert(!I2CBus_TryRead(0x28,data,8) && calls==2);
    inject(ESP_FAIL,ESP_OK,1500);assert(!I2CBus_TryRead(0x28,data,8) && calls==1);
    inject(ESP_ERR_TIMEOUT,ESP_OK,2000);assert(!I2CBus_TryRead(0x28,data,8) && calls==1);
    assert(I2CBus_GetDiagnostics(0x28,&d) && d.results[I2C_RESULT_TIMEOUT]==1 && d.max_us==2000);
    inject(ESP_ERR_INVALID_STATE,ESP_OK,100);assert(!I2CBus_TryRead(0x28,data,8) && calls==1);
    inject(ESP_FAIL,ESP_OK,100);assert(!I2CBus_Write(0x20,data,2) && calls==1);
    assert(I2CBus_GetDiagnostics(0x20,&d) && d.results[I2C_RESULT_NACK]==1 && d.retries==0);
    for (int i=0;i<1000;i++) {
        inject(ESP_ERR_TIMEOUT,ESP_ERR_TIMEOUT,10000);
        assert(!I2CBus_Write(0x20,data,2) && calls==1);
    }
    assert(I2CBus_GetDiagnostics(0x20,&d) && d.results[I2C_RESULT_TIMEOUT]==1000);
    inject(ESP_OK,ESP_OK,100);acquisition_us=350;
    assert(I2CBus_ReadRegister(0x20,1,data,1));acquisition_us=0;
    assert(I2CBus_GetDiagnostics(0x20,&d) && d.last_wait_us==350 && d.last_us==100);
    inject(ESP_FAIL,ESP_OK,100);
    assert(!I2CBus_ReadRegister(0x20,1,data,1) && calls==1); /* no readback retry */
    inject(ESP_FAIL,ESP_OK,100);
    assert(!I2CBus_TryReadOnce(0x28,data,4) && calls==1);
    char wire[2048];assert(I2CBus_FormatDiagnostics(wire,sizeof(wire))>0);
    assert(I2CBus_FormatDiagnostics(wire,8)==-1);
    wire_model=true;int64_t begin=now;
    for(unsigned slot=0;slot<100;slot++) {
        inject(ESP_OK,ESP_OK,0);assert(I2CBus_TryRead(0x28,data,4));
        inject(ESP_OK,ESP_OK,0);assert(I2CBus_ReadRegister(0x20,1,data,1));
    }
    printf("MODEL hz=%u 100 sensor reads + 100 latch probes: bus_us=%lld load=%.3f%% (wire-only fake, not hardware)\n",
        SENSOR_I2C_HZ,(long long)(now-begin),(double)(now-begin)/10000.0);
    assert(now-begin==(int64_t)100*(((45*1000000+SENSOR_I2C_HZ-1)/SENSOR_I2C_HZ)+(36*1000000/SENSOR_I2C_HZ)));
    assert(!I2CBus_GetDiagnostics(128,&d) && !I2CBus_GetDiagnostics(0,NULL));
    puts("PASS I2C busy/NACK/timeout/driver classification, per-address timing, bounded read retry, no DO write retries, persistent outage");
    return 0;
}
