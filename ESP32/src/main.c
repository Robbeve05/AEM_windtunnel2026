/* Author: R.E. van Eekelen */
#include "ApplicationTasks.h"
#include "esp_ota_ops.h"
#include "esp_log.h"

void app_main(void)
{
    const esp_partition_t *running=esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (running && esp_ota_get_state_partition(running,&state)==ESP_OK &&
        state==ESP_OTA_IMG_PENDING_VERIFY) {
        /* Keep rollback enabled but accept a booted image after it reaches
         * app_main. Full bench acceptance still requires operator checks.
         * Motor starts independently inhibited by safety commissioning. */
        ESP_ERROR_CHECK(esp_ota_mark_app_valid_cancel_rollback());
    }
    StartApplicationTasks();
}
