/* Author: R.E. van Eekelen. Core 0 only; never called by ControlTask. */
#include "I2CBus.h"
#include "AppConfig.h"
#include "SafetyIO.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/i2c.h"
static SemaphoreHandle_t mutex;
static bool ready;
bool I2CBus_Write(uint8_t a,const uint8_t *p,size_t n) {
    if(!ready||xSemaphoreTake(mutex,pdMS_TO_TICKS(I2C_TIMEOUT_MS))!=pdTRUE) return false;
    bool ok=i2c_master_write_to_device(0,a,p,n,pdMS_TO_TICKS(I2C_TIMEOUT_MS))==ESP_OK;
    xSemaphoreGive(mutex); return ok;
}
bool I2CBus_Read(uint8_t a,uint8_t *p,size_t n) {
    if(!ready||xSemaphoreTake(mutex,pdMS_TO_TICKS(I2C_TIMEOUT_MS))!=pdTRUE) return false;
    bool ok=i2c_master_read_from_device(0,a,p,n,pdMS_TO_TICKS(I2C_TIMEOUT_MS))==ESP_OK;
    xSemaphoreGive(mutex); return ok;
}
/* Sensor slots never wait for the output task to release the mutex. */
bool I2CBus_TryRead(uint8_t a,uint8_t *p,size_t n) {
    if(!ready || xSemaphoreTake(mutex,0)!=pdTRUE) return false;
    bool ok=i2c_master_read_from_device(0,a,p,n,pdMS_TO_TICKS(I2C_TIMEOUT_MS))==ESP_OK;
    xSemaphoreGive(mutex); return ok;
}
bool I2CBus_TryWrite(uint8_t a,const uint8_t *p,size_t n) {
    if(!ready || xSemaphoreTake(mutex,0)!=pdTRUE) return false;
    bool ok=i2c_master_write_to_device(0,a,p,n,pdMS_TO_TICKS(I2C_TIMEOUT_MS))==ESP_OK;
    xSemaphoreGive(mutex); return ok;
}
bool I2CBus_Init(void) {
#if SIMULATION_MODE
    return DigitalOutputs_Init();
#else
    mutex=xSemaphoreCreateMutex(); if(!mutex) return false;
    const i2c_config_t c={.mode=I2C_MODE_MASTER,.sda_io_num=SENSOR_I2C_SDA_PIN,
        .scl_io_num=SENSOR_I2C_SCL_PIN,.sda_pullup_en=true,.scl_pullup_en=true,.master.clk_speed=SENSOR_I2C_HZ};
    ready=i2c_param_config(0,&c)==ESP_OK&&i2c_driver_install(0,I2C_MODE_MASTER,0,0,0)==ESP_OK;
    bool ok=ready&&DigitalOutputs_Init();
    if(!ok) ready=false;
    return ok;
#endif
}
