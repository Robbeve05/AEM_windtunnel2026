///////////////////////////////////////////////////////////////////////////////
// File information

/*
 * ApplicationTasks.h
 * Shared startup and advisory event interfaces.
 *
 * Author: R.E. van Eekelen
 */
///////////////////////////////////////////////////////////////////////////////
// Include guard
#pragma once

///////////////////////////////////////////////////////////////////////////////
// FreeRTOS includes
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

///////////////////////////////////////////////////////////////////////////////
// Public definitions

#define EVT_START_BUTTON (1U << 0)
#define EVT_STOP_BUTTON (1U << 1)
#define EVT_RESET_BUTTON (1U << 2)
#define EVT_ALL_BUTTONS (EVT_START_BUTTON | EVT_STOP_BUTTON | EVT_RESET_BUTTON)

/* Sticky initialization completion bits, independent of runtime health. */
#define EVT_INPUT_TASK_READY (1U << 0)
#define EVT_SENSOR_TASK_READY (1U << 1)
#define EVT_PI_TASK_READY (1U << 2)
#define EVT_ALL_TASKS_READY (EVT_INPUT_TASK_READY | EVT_SENSOR_TASK_READY | EVT_PI_TASK_READY)

/* Advisory only: queues and freshness checks remain authoritative. */
#define EVT_NEW_COMMAND (1U << 0)
#define EVT_SENSOR_UPDATED (1U << 1)

/* InputHandlerTask produces physical actions; ControlTask alone consumes them. */
///////////////////////////////////////////////////////////////////////////////
// Public declarations

#ifdef __cplusplus
extern "C"
{
#endif
    extern EventGroupHandle_t handle_ButtonEventGroup;
    extern EventGroupHandle_t handle_ThreadEventGroup;
    extern EventGroupHandle_t handle_SystemEventGroup;
    extern TaskHandle_t input_task_handle;
    extern TaskHandle_t pi_task_handle;
    extern TaskHandle_t heartbeat_task_handle;
    extern volatile bool ota_task_alive;
    extern volatile bool ota_valid_task_alive;
#ifdef __cplusplus
}
#endif

///////////////////////////////////////////////////////////////////////////////
// Function declarations

#ifdef __cplusplus
extern "C"
{
#endif
    void StartApplicationTasks(void);
#ifdef __cplusplus
}
#endif
