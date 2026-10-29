/*
 * ApplicationTasks.h
 * Shared startup and advisory event interfaces.
 *
 * Author: R.E. van Eekelen
 */
#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#ifdef __cplusplus
extern "C"
{
#endif

///////////////////////////////////////////////////////////////////////////////
// Public event definitions

/* InputHandlerTask produces physical actions; ControlTask alone consumes them. */
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

    ///////////////////////////////////////////////////////////////////////////////
    // Shared handles

    extern EventGroupHandle_t handle_ButtonEventGroup;
    extern EventGroupHandle_t handle_ThreadEventGroup;
    extern EventGroupHandle_t handle_SystemEventGroup;

    ///////////////////////////////////////////////////////////////////////////////
    // Function prototypes

    void StartApplicationTasks(void);
#ifdef __cplusplus
}
#endif
