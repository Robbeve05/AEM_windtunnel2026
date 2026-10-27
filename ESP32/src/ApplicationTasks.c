#include "ApplicationTasks.h"
#include "TaskData.h"
#include "PiProtocol.h"
#include "freertos/task.h"
#include "esp_err.h"

QueueHandle_t commands, sensors, motor_requests, motor_status, telemetry;
EventGroupHandle_t events;
TaskHandle_t control_task_handle;
void StartApplicationTasks(void)
{
    commands = xQueueCreate(16, sizeof(PiCommand));
    sensors = xQueueCreate(1, sizeof(Sample));
    motor_requests = xQueueCreate(1, sizeof(MotorRequest));
    motor_status = xQueueCreate(1, sizeof(MotorStatus));
    telemetry = xQueueCreate(1, sizeof(Telemetry));
    events = xEventGroupCreate();
    ESP_ERROR_CHECK(commands && sensors && motor_requests && motor_status && telemetry && events
                    ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(SafetyTask,"SafetyTask",3072,NULL,5,NULL,0)==pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(SensorTask, "SensorTask", 4096, NULL, 3, NULL, 0)==pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(ActuatorTask, "ActuatorTask", 4096, NULL, 3, NULL, 0)==pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(ControlTask, "ControlTask", 4096, NULL, 4,
        &control_task_handle, 1)==pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(PiCommunicationTask, "PiCommunication", 6144, NULL, 2, NULL, 0)==pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}
