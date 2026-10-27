#include "AppConfig.h"
#include "WindtunnelHAL.h"
#if !SIMULATION_MODE
#include "driver/i2c.h"
#include "esp_timer.h"
#include <math.h>
#include <string.h>

/* SensorTask exclusively owns the shared bus, including the onboard expander. */
static Sample latest;
static float zero, filtered;
static unsigned zero_count;
static double zero_sum;
static int64_t previous;
static bool read_bytes(uint8_t address, uint8_t *data, size_t count)
{
    return i2c_master_read_from_device(I2C_NUM_0,address,data,count,
        pdMS_TO_TICKS(I2C_TIMEOUT_MS)) == ESP_OK;
}
#if PRESSURE_TAPS_ENABLED
static bool select_tap(int index)
{
    /* Both multiplexers must be off before selecting exactly one channel. */
    uint8_t off=0;
    bool a=i2c_master_write_to_device(0,MUX_0_ADDRESS,&off,1,pdMS_TO_TICKS(I2C_TIMEOUT_MS))==ESP_OK;
    bool b=i2c_master_write_to_device(0,MUX_1_ADDRESS,&off,1,pdMS_TO_TICKS(I2C_TIMEOUT_MS))==ESP_OK;
    if (!a || !b || index<0) return a && b;
    uint8_t channel=1u << (index%8);
    return i2c_master_write_to_device(0,index<8 ? MUX_0_ADDRESS : MUX_1_ADDRESS,
        &channel,1,pdMS_TO_TICKS(I2C_TIMEOUT_MS))==ESP_OK;
}
static void read_taps(void)
{
    static const uint8_t trigger[]={0xaa,0,0};
    uint16_t started=0;
    latest.taps_valid=0;
    for (int i=0;i<16;i++) {
        latest.taps[i]=NAN;
        if (select_tap(i) && i2c_master_write_to_device(0,ABP2_ADDRESS,trigger,3,
            pdMS_TO_TICKS(I2C_TIMEOUT_MS))==ESP_OK) started |= 1u<<i;
        else latest.errors++;
    }
    select_tap(-1);
    vTaskDelay(pdMS_TO_TICKS(5)+1);
    for (int i=0;i<16;i++) {
        uint8_t data[4];
        if (!(started & (1u<<i))) continue;
        if (!select_tap(i) || !read_bytes(ABP2_ADDRESS,data,4) ||
            (data[0]&0x25) || !(data[0]&0x40)) { latest.errors++; continue; }
        uint32_t raw=((uint32_t)data[1]<<16)|((uint32_t)data[2]<<8)|data[3];
        if (raw<ABP2_OUTPUT_MIN || raw>ABP2_OUTPUT_MAX) { latest.errors++; continue; }
        latest.taps[i]=(raw-ABP2_OUTPUT_MIN)*(ABP2_MAX_PA-ABP2_MIN_PA)/
            (ABP2_OUTPUT_MAX-ABP2_OUTPUT_MIN)+ABP2_MIN_PA;
        latest.taps_valid |= 1u<<i;
    }
    if (!select_tap(-1)) { latest.taps_valid=0; latest.errors++; }
}
#endif
bool SensorLib_Init(void)
{
    const i2c_config_t config={.mode=I2C_MODE_MASTER,
        .sda_io_num=SENSOR_I2C_SDA_PIN,.scl_io_num=SENSOR_I2C_SCL_PIN,
        .sda_pullup_en=true,.scl_pullup_en=true,.master.clk_speed=SENSOR_I2C_HZ};
    memset(&latest,0,sizeof(latest));
    if (i2c_param_config(0,&config)!=ESP_OK ||
        i2c_driver_install(0,I2C_MODE_MASTER,0,0,0)!=ESP_OK) return false;
    /* Vendor's default inactive output state: load zero before enabling outputs. */
    uint8_t off[]={1,0}, direction[]={3,0};
    return i2c_master_write_to_device(0,BOARD_DO_I2C_ADDRESS,off,2,pdMS_TO_TICKS(I2C_TIMEOUT_MS))==ESP_OK &&
        i2c_master_write_to_device(0,BOARD_DO_I2C_ADDRESS,direction,2,pdMS_TO_TICKS(I2C_TIMEOUT_MS))==ESP_OK;
}
static bool pitot(float *pressure, float *temperature)
{
    uint8_t data[4];
    if (!read_bytes(PITOT_ADDRESS,data,4) || data[0]>>6) return false;
    unsigned raw=((data[0]&0x3f)<<8)|data[1];
    if (raw<1638 || raw>14746) return false;
    *pressure=(raw-0.1f*16383)*(PITOT_MAX_PA-PITOT_MIN_PA)/(0.8f*16383)+PITOT_MIN_PA;
    *temperature=200.0f*((data[2]<<3)|(data[3]>>5))/2047.0f-50;
    return true;
}
bool SensorLib_Read(float *velocity)
{
    *velocity=0;
#if PRESSURE_TAPS_ENABLED
    if (!select_tap(-1)) { latest.errors++; return false; }
#endif
    /* Preserve the Uno's 10-reading average and pressure low-pass filter. */
    float sum=0, temps=0; unsigned count=0;
    for (unsigned i=0;i<10;i++) {
        float p,t;
        if (pitot(&p,&t)) { sum+=p; temps+=t; count++; }
        else latest.errors++;
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    if (count!=10) { previous=0; return false; }
    float pressure=sum/count;
    latest.temperature_c=temps/count;
    /* Start inhibited until 200 valid readings; keep pitot in still air at boot. */
    if (zero_count<PITOT_ZERO_SAMPLES) {
        zero_sum+=sum; zero_count+=count;
        zero=(float)(zero_sum/zero_count);
        return false;
    }
    pressure-=zero;
    int64_t now=esp_timer_get_time();
    if (!previous) filtered=pressure;
    else filtered+=(1-expf(-6.283185307f*SensorLib_FilterHz()*(now-previous)/1000000.0f))*(pressure-filtered);
    previous=now;
    latest.pressure_pa=fabsf(filtered)<PITOT_DEADBAND_PA ? 0 : filtered;
    *velocity=sqrtf(2*fabsf(latest.pressure_pa)/AIR_DENSITY);
#if PRESSURE_TAPS_ENABLED
    read_taps();
#endif
    return isfinite(*velocity);
}
void SensorLib_Details(Sample *s)
{
    s->pressure_pa=latest.pressure_pa; s->temperature_c=latest.temperature_c;
    s->errors=latest.errors; s->taps_valid=latest.taps_valid;
    memcpy(s->taps,latest.taps,sizeof(s->taps));
}
#endif
