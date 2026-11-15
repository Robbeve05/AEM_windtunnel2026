#include <assert.h>
#include <stdio.h>
#include "AppConfig.h"
#include "SafetyIO.h"
#include "ApplicationTasks.h"
#include "driver/gpio.h"
static int levels[64];
static void (*handlers[64])(void *);
static void *args[64];
static int64_t clock_us=1000000;
EventGroupHandle_t handle_ButtonEventGroup;
EventBits_t xEventGroupSetBits(EventGroupHandle_t g, EventBits_t bits) { (void)g; return bits; }
int64_t esp_timer_get_time(void) { return clock_us; }
esp_err_t gpio_config(const gpio_config_t *c) { (void)c;return ESP_OK; }
esp_err_t gpio_set_intr_type(int g,int t) { (void)g;(void)t;return ESP_OK; }
esp_err_t gpio_isr_handler_add(int g,void (*h)(void *),void *a) {handlers[g]=h;args[g]=a;return ESP_OK;}
int gpio_get_level(int g) {return levels[g];}
static void edge(int gpio,int level) { levels[gpio]=level; clock_us+=37; handlers[gpio](args[gpio]); }
int main(void) {
    levels[ESTOP_PRESSED_GPIO]=!ESTOP_PRESSED_ACTIVE_LEVEL;
    levels[SAFETY_RELAY_OK_GPIO]=SAFETY_RELAY_OK_ACTIVE_LEVEL;
    levels[STOP_BUTTON_OK_GPIO]=STOP_BUTTON_OK_ACTIVE_LEVEL;
    SafetyIO_Init();
    edge(ESTOP_PRESSED_GPIO,ESTOP_PRESSED_ACTIVE_LEVEL);
    SafetyGPIOEvent first;
    SafetyIO_GetLastEvent(&first);
    assert(first.valid && first.timestamp_us==(uint64_t)clock_us && first.generation==1);
    assert(first.emergency_latch && first.active_level==ESTOP_PRESSED_ACTIVE_LEVEL);
    assert(SafetyIO_Fault()==FAULT_EMERGENCY_STOP);
    edge(ESTOP_PRESSED_GPIO,!ESTOP_PRESSED_ACTIVE_LEVEL);
    SafetyIO_Acknowledge(); assert(SafetyIO_Fault()==FAULT_NONE);
    edge(SAFETY_RELAY_OK_GPIO,!SAFETY_RELAY_OK_ACTIVE_LEVEL);
    SafetyIO_GetLastEvent(&first); assert(first.relay_latch && first.unsafe_generation>=2);
    assert(SafetyIO_Fault()==FAULT_SAFETY_FEEDBACK);
    edge(STOP_BUTTON_OK_GPIO,!STOP_BUTTON_OK_ACTIVE_LEVEL);
    SafetyIO_GetLastEvent(&first); assert(first.stop_latch && SafetyIO_Stop());
    for(unsigned i=0;i<SAFETY_GPIO_EVENT_CAPACITY+5;i++)
        edge(STOP_BUTTON_OK_GPIO,(int)(i&1u));
    SafetyGPIOEvent events[SAFETY_GPIO_EVENT_CAPACITY]; uint32_t lost=0;
    unsigned n=SafetyIO_ReadEvents(events,SAFETY_GPIO_EVENT_CAPACITY,&lost);
    assert(n==SAFETY_GPIO_EVENT_CAPACITY && lost==9);
    for(unsigned i=1;i<n;i++) {
        assert(events[i].generation==events[i-1].generation+1);
        assert(events[i].timestamp_us>events[i-1].timestamp_us);
    }
    assert(SafetyIO_ReadEvents(events,1,&lost)==0);
    SafetyIO_GetLastEvent(&first); assert(first.generation==events[n-1].generation);
    puts("PASS safety ISR timestamps, polarity, latches, generation, bounded overflow and task-context drain");
    return 0;
}
