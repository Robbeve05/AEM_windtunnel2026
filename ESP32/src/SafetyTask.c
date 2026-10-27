#include "AppConfig.h"
#include "TaskData.h"
#include "driver/gpio.h"
#include "esp_task_wdt.h"
#include "esp_err.h"

void SafetyTask(void *argument)
{
    (void)argument;
#if INTERLOCK_DI_INDEX >= 0
    const int pins[]=BOARD_DI_PINS;
    _Static_assert(INTERLOCK_DI_INDEX<8,"Invalid interlock input");
    gpio_config_t config={.pin_bit_mask=1ULL<<pins[INTERLOCK_DI_INDEX],.mode=GPIO_MODE_INPUT};
    ESP_ERROR_CHECK(gpio_config(&config));
#endif
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    for (;;) {
#if INTERLOCK_DI_INDEX >= 0
        if (gpio_get_level(pins[INTERLOCK_DI_INDEX])!=INTERLOCK_OK_LEVEL)
            xEventGroupSetBits(events,INTERLOCK_OPEN|STOP_REQUEST);
        else xEventGroupClearBits(events,INTERLOCK_OPEN);
#endif
        MotorRequest request={0};
        if (xQueuePeek(motor_requests,&request,0)==pdTRUE && request.run &&
            xTaskGetTickCount()-request.tick>=pdMS_TO_TICKS(CONTROL_CLOCK_TIMEOUT_MS))
            xEventGroupSetBits(events,STOP_REQUEST);
        ESP_ERROR_CHECK(esp_task_wdt_reset());
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
