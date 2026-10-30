/* Author: R.E. van Eekelen. One logical image, one serialized output writer.
 * LED calls only stage bits under a short critical section. They never take the
 * I2C mutex or write an output byte, so cannot delay K2 disabling on the bus.
 */
#include "DigitalOutputs.h"
#include "AppConfig.h"
#include "I2CBus.h"
#include "SafetyIO.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include <stddef.h>
#define OUTPUT_BIT(n) (1u << (n))
#define CHECK_BIT(n) _Static_assert((n)>=0 && (n)<=7, "Output index must be 0..7")
CHECK_BIT(VFD_ENABLE_DO_BIT); CHECK_BIT(GREEN_LED_DO_BIT);
CHECK_BIT(YELLOW_LED_DO_BIT); CHECK_BIT(RED_LED_DO_BIT);
CHECK_BIT(SPARE_DO5_BIT); CHECK_BIT(SPARE_DO6_BIT);
CHECK_BIT(SPARE_DO7_BIT); CHECK_BIT(SPARE_DO8_BIT);
_Static_assert((OUTPUT_BIT(VFD_ENABLE_DO_BIT)|OUTPUT_BIT(GREEN_LED_DO_BIT)|
    OUTPUT_BIT(YELLOW_LED_DO_BIT)|OUTPUT_BIT(RED_LED_DO_BIT)|OUTPUT_BIT(SPARE_DO5_BIT)|
    OUTPUT_BIT(SPARE_DO6_BIT)|OUTPUT_BIT(SPARE_DO7_BIT)|OUTPUT_BIT(SPARE_DO8_BIT))==0xffu,
    "All eight output assignments must be unique");
_Static_assert(VFD_ENABLE_ACTIVE_LEVEL==0 || VFD_ENABLE_ACTIVE_LEVEL==1, "Invalid K2 polarity");
_Static_assert(STATUS_LED_ACTIVE_LEVEL==0 || STATUS_LED_ACTIVE_LEVEL==1, "Invalid lamp polarity");
_Static_assert(EXTERNAL_STATUS_LEDS_ENABLED==0 || EXTERNAL_STATUS_LEDS_ENABLED==1, "Invalid lamp enable");
static portMUX_TYPE state_lock=portMUX_INITIALIZER_UNLOCKED;
static SemaphoreHandle_t writer_lock;
static DigitalOutputStatus state;
static bool green_request, yellow_request, red_request;
static uint8_t compose(void) {
    uint8_t image=0; // Unassigned DO5..DO8 remain low and cannot be commanded.
    if(state.vfd_requested==(VFD_ENABLE_ACTIVE_LEVEL!=0)) image|=OUTPUT_BIT(VFD_ENABLE_DO_BIT);
    if((EXTERNAL_STATUS_LEDS_ENABLED && green_request)==(STATUS_LED_ACTIVE_LEVEL!=0)) image|=OUTPUT_BIT(GREEN_LED_DO_BIT);
    if((EXTERNAL_STATUS_LEDS_ENABLED && yellow_request)==(STATUS_LED_ACTIVE_LEVEL!=0)) image|=OUTPUT_BIT(YELLOW_LED_DO_BIT);
    if((EXTERNAL_STATUS_LEDS_ENABLED && red_request)==(STATUS_LED_ACTIVE_LEVEL!=0)) image|=OUTPUT_BIT(RED_LED_DO_BIT);
    return image;
}
static bool write_image(uint8_t image) {
#if SIMULATION_MODE
    (void)image; return true;
#else
    const uint8_t data[]={1,image};
    return I2CBus_Write(BOARD_DO_I2C_ADDRESS,data,sizeof(data));
#endif
}
bool DigitalOutputs_Init(void) {
    // Initialization is single-threaded before any producer exists.
    if(!writer_lock) writer_lock=xSemaphoreCreateMutex();
    if(!writer_lock) { ESP_LOGE("DigitalOutputs","Output mutex allocation failed"); return false; }
    portENTER_CRITICAL(&state_lock);
    state=(DigitalOutputStatus){0};
    green_request=yellow_request=red_request=false;
    const uint8_t inactive=compose();
    state.requested_image=inactive;
    portEXIT_CRITICAL(&state_lock);
    // Load inactive latches before enabling any output direction, including K2.
    bool ok=write_image(inactive);
#if !SIMULATION_MODE
    const uint8_t direction[]={3,0};
    if(ok) ok=I2CBus_Write(BOARD_DO_I2C_ADDRESS,direction,sizeof(direction));
#endif
    portENTER_CRITICAL(&state_lock);
    state.initialized=state.confirmed_valid=ok;
    state.confirmed_image=inactive;
    if(!ok) state.write_errors++;
    portEXIT_CRITICAL(&state_lock);
    if(!ok) ESP_LOGE("DigitalOutputs","Inactive image/direction initialization failed; motor inhibited");
    return ok;
}
bool DigitalOutputs_SetStatusLeds(bool green, bool yellow, bool red) {
    portENTER_CRITICAL(&state_lock);
    green_request=green; yellow_request=yellow; red_request=red;
    state.requested_image=compose();
    const bool ready=state.initialized;
    portEXIT_CRITICAL(&state_lock);
    return ready;
}
bool DigitalOutputs_Set(bool enabled) {
    // Only K2-path writers take this mutex. FreeRTOS priority inheritance applies.
    if(!writer_lock || xSemaphoreTake(writer_lock,portMAX_DELAY)!=pdTRUE) return false;
    portENTER_CRITICAL(&state_lock);
    const bool ready=state.initialized;
    portEXIT_CRITICAL(&state_lock);
    if(!ready) { xSemaphoreGive(writer_lock); return false; }
    const bool authorized=enabled && SAFETY_IO_COMMISSIONED && !SafetyIO_Inhibited();
    portENTER_CRITICAL(&state_lock);
    state.vfd_requested=authorized;
    const uint8_t image=compose();
    state.requested_image=image;
    portEXIT_CRITICAL(&state_lock);
    const bool ok=write_image(image);
    portENTER_CRITICAL(&state_lock);
    state.confirmed_valid=ok;
    if(ok) state.confirmed_image=image;
    else state.write_errors++;
    portEXIT_CRITICAL(&state_lock);
    xSemaphoreGive(writer_lock);
    return ok;
}
void DigitalOutputs_GetStatus(DigitalOutputStatus *out) {
    if(!out) return;
    portENTER_CRITICAL(&state_lock);
    *out=state;
    portEXIT_CRITICAL(&state_lock);
}
