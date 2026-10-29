#include "ApplicationTasks.h"
#include "SafetyIO.h"
#include "I2CBus.h"
#include "TaskData.h"
#include "PiProtocol.h"
#include "OtaUpdate.h"
void HeartbeatTask(void *argument);
#include "freertos/task.h"
#include "esp_err.h"

QueueHandle_t commands, sensors, motor_requests, motor_status, telemetry, safety_status, pi_heartbeat, tap_samples;
EventGroupHandle_t events;
TaskHandle_t control_task_handle, sensor_task_handle;
void StartApplicationTasks(void)
{
    SafetyIO_Init();
    (void)I2CBus_Init(); /* Failed I/O remains inhibited through output diagnostics. */
    safety_status=xQueueCreate(1,sizeof(SafetySnapshot));
    pi_heartbeat=xQueueCreate(1,sizeof(PiHeartbeat));
    commands = xQueueCreate(16, sizeof(PiCommand));
    sensors = xQueueCreate(1, sizeof(Sample));
    tap_samples = xQueueCreate(1, sizeof(Sample));
    motor_requests = xQueueCreate(1, sizeof(MotorRequest));
    motor_status = xQueueCreate(1, sizeof(MotorStatus));
    telemetry = xQueueCreate(1, sizeof(Telemetry));
    events = xEventGroupCreate();
    ESP_ERROR_CHECK(commands && sensors && motor_requests && motor_status && telemetry && events && safety_status && pi_heartbeat && tap_samples
                    ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(SafetyTask,"SafetyTask",3072,NULL,5,NULL,0)==pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(SensorTask, "SensorTask", 4096, NULL, 3, &sensor_task_handle, 0)==pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(ControlTask, "ControlTask", 6144, NULL, 4,
        &control_task_handle, 1)==pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(HeartbeatTask,"Heartbeat",4096,NULL,1,NULL,0)==pdPASS?ESP_OK:ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(OtaMarkValidTask, "OtaVerify", 3072, NULL, 1, NULL, 0)==pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(OtaUpdateTask, "OtaUpdate", 6144, NULL, 1, NULL, 0)==pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(PiCommunicationTask, "PiCommunication", 6144, NULL, 2, NULL, 0)==pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}
