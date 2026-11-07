///////////////////////////////////////////////////////////////////////////////
// File information

/*
 * ApplicationTasks.c
 * Creates communication infrastructure before starting any application task.
 *
 * Author: R.E. van Eekelen
 */

///////////////////////////////////////////////////////////////////////////////
// System includes

#include "esp_err.h"
#include "esp_log.h"

///////////////////////////////////////////////////////////////////////////////
// FreeRTOS includes

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

///////////////////////////////////////////////////////////////////////////////
// Application includes

#include "AppConfig.h"
#include "ApplicationTasks.h"
#include "FanCalibration.h"
#include "I2CBus.h"
#include "OtaUpdate.h"
#include "PiProtocol.h"
#include "SafetyIO.h"
#include "TaskData.h"

///////////////////////////////////////////////////////////////////////////////
// File-local variables

QueueHandle_t commands, sensors, motor_requests, motor_status, telemetry, safety_status,
    pi_heartbeat;
/* Retained separately for existing STOP_REQUEST producers. */
EventGroupHandle_t events;
EventGroupHandle_t handle_ButtonEventGroup;
EventGroupHandle_t handle_ThreadEventGroup;
EventGroupHandle_t handle_SystemEventGroup;
TaskHandle_t control_task_handle;
TaskHandle_t sensor_task_handle;
TaskHandle_t input_task_handle, pi_task_handle, heartbeat_task_handle;
volatile bool ota_task_alive, ota_valid_task_alive;

///////////////////////////////////////////////////////////////////////////////
// Function prototypes

void HeartbeatTask(void *argument);
static void InitHardware(void);
static void CreateQueues(void);
static void CreateEventGroups(void);
static void CreateTasks(void);

///////////////////////////////////////////////////////////////////////////////
// Initialization

///////////////////////////////////////////////////////////////////////////////
// InitHardware()

/*
 * Configures physical inputs and loads inactive output latches before tasks
 * start.
 */
static void InitHardware(void)
{
    ESP_LOGI("Startup", "Safety input initialization begin");
    SafetyIO_Init();
    ESP_LOGI("Startup", "Safety input initialization complete");
    ESP_LOGI("Startup",
             "Commissioning flags safety_io=%d v20_run=%d torque_to_v20=%d "
             "remote_start=%d",
             SAFETY_IO_COMMISSIONED, V20_RUN_COMMISSIONED, MOTOR_MODEL_TORQUE_TO_V20_ENABLED,
             ALLOW_REMOTE_START);
    ESP_LOGI("Startup", "I2C and inactive output initialization begin");
    if (!I2CBus_Init())
    {
        ESP_LOGE("ApplicationTasks", "I2C/output initialization failed; outputs remain inhibited");
    }
    else
    {
        ESP_LOGI("Startup", "I2C and inactive output initialization successful");
    }
}

///////////////////////////////////////////////////////////////////////////////
// Module-specific functionality

///////////////////////////////////////////////////////////////////////////////
// CreateQueues()

/*
 * Creates the existing command and one-element latest-value queues.
 */
static void CreateQueues(void)
{
    safety_status = xQueueCreate(1, sizeof(SafetySnapshot));
    pi_heartbeat = xQueueCreate(1, sizeof(PiHeartbeat));
    commands = xQueueCreate(16, sizeof(PiCommand));
    sensors = xQueueCreate(1, sizeof(Sample));
    motor_requests = xQueueCreate(1, sizeof(MotorRequest));
    motor_status = xQueueCreate(1, sizeof(MotorStatus));
    telemetry = xQueueCreate(1, sizeof(Telemetry));
    ESP_ERROR_CHECK(commands && sensors && motor_requests && motor_status && telemetry &&
                            safety_status && pi_heartbeat
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);
}

///////////////////////////////////////////////////////////////////////////////
// CreateEventGroups()

/*
 * Creates all event interfaces before any producer or consumer can run.
 */
static void CreateEventGroups(void)
{
    events = xEventGroupCreate();
    handle_ButtonEventGroup = xEventGroupCreate();
    handle_ThreadEventGroup = xEventGroupCreate();
    handle_SystemEventGroup = xEventGroupCreate();
    ESP_ERROR_CHECK(events && handle_ButtonEventGroup && handle_ThreadEventGroup &&
                            handle_SystemEventGroup
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);
}

///////////////////////////////////////////////////////////////////////////////
// CreateTasks()

/*
 * Preserves task stacks, priorities, CPU assignments and the startup barrier.
 */
static void CreateTasks(void)
{
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(InputHandlerTask, "InputHandler", 3072, NULL, 5,
                                            &input_task_handle, 0) == pdPASS
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);
    ESP_LOGI("Startup", "Created InputHandler task");
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(SensorTask, "SensorTask", 4096, NULL, 3,
                                            &sensor_task_handle, 0) == pdPASS
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);
    ESP_LOGI("Startup", "Created SensorTask");
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(ControlTask, "ControlTask", 6144, NULL, 4,
                                            &control_task_handle, 1) == pdPASS
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);
    ESP_LOGI("Startup", "Created ControlTask");
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(PiCommunicationTask, "PiCommunication", 8192, NULL, 2,
                                            &pi_task_handle, 0) == pdPASS
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);
    ESP_LOGI("Startup", "Created PiCommunication task");
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(HeartbeatTask, "Heartbeat", 3072, NULL, 1,
                                            &heartbeat_task_handle, 0) == pdPASS
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);
    ESP_LOGI("Startup", "Created Heartbeat task");
    ota_task_alive = true;
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(OtaUpdateTask, "OTA", 8192, NULL, 1, NULL, 0) == pdPASS
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);
    ESP_LOGI("Startup", "Created OTA task");
    ota_valid_task_alive = true;
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(OtaMarkValidTask, "OTAValid", 3072, NULL, 1, NULL, 0) ==
                            pdPASS
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);
    ESP_LOGI("Startup", "Created OTAValid task");
}

///////////////////////////////////////////////////////////////////////////////
// StartApplicationTasks()

/*
 * Initializes hardware and shared communication before creating application
 * tasks.
 */
void StartApplicationTasks(void)
{
    InitHardware();
    (void)FanCalibration_Init();
    ESP_LOGI("Startup", "Hardware initialization returned; creating queues");
    CreateQueues();
    ESP_LOGI("Startup", "Queues created; creating event groups");
    CreateEventGroups();
    ESP_LOGI("Startup", "Event groups created; creating application tasks");
    CreateTasks();
}
