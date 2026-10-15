#include "ApplicationTasks.h"
#include "TaskData.h"
#include "PiProtocol.h"
#include "freertos/task.h"
#include "esp_err.h"

QueueHandle_t commands, sensors, motor_requests, motor_status, telemetry;
EventGroupHandle_t events;
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
    ESP_ERROR_CHECK(xTaskCreate(SensorTask, "SensorTask", 3072, NULL, 3, NULL)==pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreate(ActuatorTask, "ActuatorTask", 3072, NULL, 3, NULL)==pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreate(ControlTask, "ControlTask", 4096, NULL, 4, NULL)==pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreate(PiCommunicationTask, "PiCommunication", 4096, NULL, 2, NULL)==pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}
