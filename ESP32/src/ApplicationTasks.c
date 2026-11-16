///////////////////////////////////////////////////////////////////////////////
// File information

/*
 * ApplicationTasks.c
 *
 * Creates communication infrastructure before starting any application task.
 *
 * Author: R.E. van Eekelen
 */

///////////////////////////////////////////////////////////////////////////////
// System includes

#include "esp_err.h"
#include "esp_heap_caps.h"
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
#include "FaultRecorder.h"
#include "I2CBus.h"
#include "OtaUpdate.h"
#include "PiProtocol.h"
#include "SafetyIO.h"
#include "TaskData.h"

///////////////////////////////////////////////////////////////////////////////
// File-local variables

QueueHandle_t commands, pitot_samples, pressure_taps, motor_requests, motor_status, telemetry, safety_status,
    pi_heartbeat;
/* Retained separately for existing STOP_REQUEST producers. */
EventGroupHandle_t events;
EventGroupHandle_t handle_ButtonEventGroup;
EventGroupHandle_t handle_ThreadEventGroup;
EventGroupHandle_t handle_SystemEventGroup;
TaskHandle_t control_task_handle;
TaskHandle_t sensor_task_handle;
TaskHandle_t input_task_handle, pi_task_handle, heartbeat_task_handle, fault_diagnostics_task_handle;
volatile bool ota_task_alive, ota_valid_task_alive;

///////////////////////////////////////////////////////////////////////////////
// Function prototypes

void HeartbeatTask(void *argument);
static void InitHardware(void);
static void CreateQueues(void);
static void CreateEventGroups(void);
static void CreateTasks(void);
static void CreateTask(TaskFunction_t entry, const char *name, uint32_t stack_bytes,
                       UBaseType_t priority, TaskHandle_t *handle, BaseType_t core);

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
    SafetyIO_Init();
    ESP_LOGI("Startup",
             "Commissioning flags safety_io=%d v20_run=%d torque_to_v20=%d "
             "remote_start=%d",
             SAFETY_IO_COMMISSIONED, V20_RUN_COMMISSIONED, MOTOR_MODEL_TORQUE_TO_V20_ENABLED,
             ALLOW_REMOTE_START);
    if (!I2CBus_Init())
    {
        ESP_LOGE("ApplicationTasks", "I2C/output initialization failed; outputs remain inhibited");
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
    pitot_samples = xQueueCreate(1, sizeof(PitotSample));
    pressure_taps = xQueueCreate(1, sizeof(PressureTapSample));
    motor_requests = xQueueCreate(1, sizeof(MotorRequest));
    motor_status = xQueueCreate(1, sizeof(MotorStatus));
    telemetry = xQueueCreate(1, sizeof(Telemetry));
    ESP_ERROR_CHECK(commands && pitot_samples && pressure_taps && motor_requests && motor_status && telemetry &&
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
// CreateTask()

/*
 * Checks allocation before proceeding and identifies the failing task.
 */
static void CreateTask(TaskFunction_t entry, const char *name, uint32_t stack_bytes,
                       UBaseType_t priority, TaskHandle_t *handle, BaseType_t core)
{
    BaseType_t result = xTaskCreatePinnedToCore(entry, name, stack_bytes, NULL, priority, handle, core);
    if (result != pdPASS)
        ESP_LOGE("Startup", "Task %s creation failed: result=%ld stack_bytes=%lu "
                 "internal_free=%u internal_largest=%u", name, (long)result,
                 (unsigned long)stack_bytes,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    ESP_ERROR_CHECK(result == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_LOGD("Startup", "Created %s stack_bytes=%lu internal_free=%u internal_largest=%u",
             name, (unsigned long)stack_bytes,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
}

///////////////////////////////////////////////////////////////////////////////
// CreateTasks()

static void CreateTasks(void)
{
    FaultRecorder_Reset();
    CreateTask(InputHandlerTask, "InputHandler", 3072, 5, &input_task_handle, 0);
    CreateTask(SensorTask, "SensorTask", 4096, 3, &sensor_task_handle, 0);
    CreateTask(ControlTask, "ControlTask", 6144, 4, &control_task_handle, 1);
    CreateTask(PiCommunicationTask, "PiCommunication", 8192, 2, &pi_task_handle, 0);
    CreateTask(HeartbeatTask, "Heartbeat", HEARTBEAT_STACK_BYTES, 1, &heartbeat_task_handle, 0);
    ota_task_alive = true;
    CreateTask(OtaUpdateTask, "OTA", 8192, 1, NULL, 0);
    ota_valid_task_alive = true;
    CreateTask(OtaMarkValidTask, "OTAValid", 3072, 1, NULL, 0);
    CreateTask(FaultDiagnosticsTask, "FaultDiagnostics", 4096, 1, &fault_diagnostics_task_handle, 0);
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
    CreateQueues();
    CreateEventGroups();
    CreateTasks();
}
