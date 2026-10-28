/* Author: R.E. van Eekelen. Shared Core 0/Core 1 I2C bus; short Core 1 pitot transactions. */
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
/* No mutex wait: never hold up the 200 Hz controller behind Core 0. 
 * ESP-IDF's legacy I2C driver still has a finite hardware timeout: verify 
 * the real execution time and overrun counters on the board. */
bool I2CBus_TryRead(uint8_t a, uint8_t *p, size_t n) {
    if (!ready || mutex == NULL || p == NULL || n == 0 ||
        xSemaphoreTake(mutex, 0) != pdTRUE)
        return false;
    const esp_err_t result = i2c_master_read_from_device(
        I2C_NUM_0, a, p, n, pdMS_TO_TICKS(PITOT_I2C_TIMEOUT_MS));
    xSemaphoreGive(mutex);
    return result == ESP_OK;
}
/* Core 0: one non-waiting pressure-tap I2C operation per phase. */
bool I2CBus_TryWrite(uint8_t a, const uint8_t *p, size_t n) {
    if (!ready || mutex == NULL || p == NULL || n == 0 ||
        xSemaphoreTake(mutex, 0) != pdTRUE)
        return false;
    const esp_err_t result = i2c_master_write_to_device(
        I2C_NUM_0, a, p, n, pdMS_TO_TICKS(PITOT_I2C_TIMEOUT_MS));
    xSemaphoreGive(mutex);
    return result == ESP_OK;
}
bool DigitalOutputs_Set(bool enabled) {
#if SIMULATION_MODE
    (void)enabled; return true;
#else
    uint8_t outputs=0;
#if VFD_ENABLE_DO_BIT >= 0 && VFD_ENABLE_DO_BIT <= 7 && VFD_ENABLE_ACTIVE_LEVEL >= 0
    enabled=enabled&&SAFETY_IO_COMMISSIONED&&!SafetyIO_Inhibited();
    if(enabled==(VFD_ENABLE_ACTIVE_LEVEL!=0)) outputs|=1u<<VFD_ENABLE_DO_BIT;
#else
    (void)enabled;
#endif
    const uint8_t data[]={1,outputs};
    return I2CBus_Write(BOARD_DO_I2C_ADDRESS,data,sizeof(data));
#endif
}
bool I2CBus_Init(void) {
#if SIMULATION_MODE
    return true;
#else
    mutex=xSemaphoreCreateMutex(); if(!mutex) return false;
    const i2c_config_t c={.mode=I2C_MODE_MASTER,.sda_io_num=SENSOR_I2C_SDA_PIN,
        .scl_io_num=SENSOR_I2C_SCL_PIN,.sda_pullup_en=true,.scl_pullup_en=true,.master.clk_speed=SENSOR_I2C_HZ};
    ready=i2c_param_config(0,&c)==ESP_OK&&i2c_driver_install(0,I2C_MODE_MASTER,0,0,0)==ESP_OK;
    /* Load the inactive latch before enabling outputs. Unassigned pins stay low. */
    uint8_t direction[]={3,0};
    uint8_t off[]={1,0};
    bool ok=ready&&I2CBus_Write(BOARD_DO_I2C_ADDRESS,off,2)&&I2CBus_Write(BOARD_DO_I2C_ADDRESS,direction,2);
    if(!ok) ready=false;
    return ok;
#endif
}
