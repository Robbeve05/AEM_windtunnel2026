#include "ApplicationTasks.h"
#include "AppConfig.h"
#include "DigitalOutputs.h"
#include "Ethernet.h"
#include "StatusOutput.h"
#include "SafetyIO.h"
#include "TaskData.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

static const char *task_state_name(TaskHandle_t task)
{
    if (task == NULL)
        return "missing";
    switch (eTaskGetState(task))
    {
    case eRunning: return "running";
    case eReady: return "ready";
    case eBlocked: return "blocked";
    case eSuspended: return "suspended";
    case eDeleted: return "deleted";
    default: return "unknown";
    }
}

static unsigned task_stack_free(TaskHandle_t task)
{
    return task == NULL ? 0u : (unsigned)uxTaskGetStackHighWaterMark(task);
}

static uint32_t ticks_age_ms(TickType_t timestamp)
{
    return (uint32_t)(xTaskGetTickCount() - timestamp) * portTICK_PERIOD_MS;
}

static unsigned raw_di_level(const SafetySnapshot *safety, bool available, unsigned di)
{
    return available ? (unsigned)((safety->raw_levels >> (di - 1u)) & 1u) : 0u;
}

static const char *interpreted_di(bool available, unsigned raw, int active_level,
                                  const char *active, const char *inactive)
{
    if (!available)
        return "unavailable";
    bool is_active = false;
    if (!SafetyIO_InterpretLevel((int)raw, active_level, &is_active))
        return "unknown_polarity";
    return is_active ? active : inactive;
}

static void LogApplicationStatus(void)
{
    const EventBits_t ready = handle_ThreadEventGroup
                                  ? xEventGroupGetBits(handle_ThreadEventGroup)
                                  : 0;
    const bool queues_ready = commands && sensors && motor_requests && motor_status &&
                              telemetry && safety_status && pi_heartbeat;
    Telemetry state = {0};
    const bool state_available = telemetry && xQueuePeek(telemetry, &state, 0) == pdTRUE;
    ESP_LOGI("Diag", "init queues=%d input=%d sensor=%d pi_listener=%d control=%d timer=%d "
                     "(ready_bits=0x%02lx expected=0x%02x)",
             queues_ready, !!(ready & EVT_INPUT_TASK_READY), !!(ready & EVT_SENSOR_TASK_READY),
             !!(ready & EVT_PI_TASK_READY), state_available && state.control_ready,
             state_available && state.control_clock_started,
             (unsigned long)ready, (unsigned)EVT_ALL_TASKS_READY);

    ESP_LOGI("Diag", "tasks input=%s/%u sensor=%s/%u control=%s/%u pi=%s/%u",
             task_state_name(input_task_handle), task_stack_free(input_task_handle),
             task_state_name(sensor_task_handle), task_stack_free(sensor_task_handle),
             task_state_name(control_task_handle), task_stack_free(control_task_handle),
             task_state_name(pi_task_handle), task_stack_free(pi_task_handle));
    ESP_LOGI("Diag", "tasks heartbeat=%s/%u ota_alive=%d ota_valid_alive=%d",
             task_state_name(heartbeat_task_handle), task_stack_free(heartbeat_task_handle),
             ota_task_alive, ota_valid_task_alive);

    Sample sample = {0};
    const bool sample_available = sensors && xQueuePeek(sensors, &sample, 0) == pdTRUE;
    MotorStatus motor = {0};
    const bool motor_available = motor_status && xQueuePeek(motor_status, &motor, 0) == pdTRUE;
    PiHeartbeat heartbeat = {0};
    const bool heartbeat_available = pi_heartbeat && xQueuePeek(pi_heartbeat, &heartbeat, 0) == pdTRUE;
    SafetySnapshot safety = {0};
    const bool safety_available = safety_status && xQueuePeek(safety_status, &safety, 0) == pdTRUE;
    const uint32_t safety_age_ms = safety_available
                                       ? (uint32_t)(esp_timer_get_time() / 1000) - safety.tick_ms
                                       : UINT32_MAX;

    if (state_available) {
        ESP_LOGI("Diag", "machine state=%s fault=%s last_fault=%s running=%d transitions=%lu "
                         "control_hz=%.1f control_ready=%d clock_started=%d",
                 StateMachine_Name(state.state), FaultReason_Name(state.fault),
                 FaultReason_Name(state.last_fault), state.running,
                 (unsigned long)state.transitions, (double)state.control_hz,
                 state.control_ready, state.control_clock_started);
    } else {
        ESP_LOGW("Diag", "machine state unavailable; ControlTask has not published telemetry");
    }

    ESP_LOGI("Diag", "safety snapshot=%d valid=%d commissioned=%d estop=%d relay_ok=%d "
                     "stop_ok=%d start=%d reset=%d vfd_fault=%d contactor_ok=%d "
                     "outputs_ok=%d fresh=%d age_ms=%lu raw_di=0x%08lx",
             safety_available, safety.valid, safety.commissioned, safety.estop_pressed,
             safety.relay_ok, safety.stop_ok, safety.start_pressed, safety.reset_pressed,
             safety.vfd_fault, safety.contactor_ok, safety.outputs_ok,
             safety_available && safety_age_ms < INPUT_TIMEOUT_MS,
             (unsigned long)safety_age_ms,
             (unsigned long)safety.raw_levels);
    ESP_LOGI("Diag", "DI raw levels available=%d: DI1/GPIO4=%u DI2/GPIO5=%u DI3/GPIO6=%u DI4/GPIO7=%u "
                     "DI5/GPIO8=%u DI6/GPIO9=%u DI7/GPIO10=%u DI8/GPIO11=%u",
             safety_available,
             raw_di_level(&safety, safety_available, 1),
             raw_di_level(&safety, safety_available, 2),
             raw_di_level(&safety, safety_available, 3),
             raw_di_level(&safety, safety_available, 4),
             raw_di_level(&safety, safety_available, 5),
             raw_di_level(&safety, safety_available, 6),
             raw_di_level(&safety, safety_available, 7),
             raw_di_level(&safety, safety_available, 8));
    ESP_LOGI("Diag", "DI interpretation snapshot_valid=%d safety_valid=%d: "
                     "DI1 E_STOP_PRESSED=%s(level=%d) DI2 RESET_PRESSED=%s(level=%d) "
                     "DI3 START_PRESSED=%s(level=%d)",
             safety_available, safety.valid,
             interpreted_di(safety_available, raw_di_level(&safety, safety_available, 1),
                            ESTOP_PRESSED_ACTIVE_LEVEL, "pressed", "released"),
             ESTOP_PRESSED_ACTIVE_LEVEL,
             interpreted_di(safety_available, raw_di_level(&safety, safety_available, 2),
                            RESET_BUTTON_ACTIVE_LEVEL, "pressed", "released"),
             RESET_BUTTON_ACTIVE_LEVEL,
             interpreted_di(safety_available, raw_di_level(&safety, safety_available, 3),
                            START_BUTTON_ACTIVE_LEVEL, "pressed", "released"),
             START_BUTTON_ACTIVE_LEVEL);
    ESP_LOGI("Diag", "DI functions: DI4 STOP_OK=%s(level=%d) DI5 SAFETY_RELAY_OK=%s(level=%d) "
                     "DI6 VFD_FAULT=%s(fitted=%d level=%d) DI7 CONTACTOR_OK=%s(fitted=%d level=%d) "
                     "DI8 SPARE",
             interpreted_di(safety_available, raw_di_level(&safety, safety_available, 4),
                            STOP_BUTTON_OK_ACTIVE_LEVEL, "ok", "not_ok"),
             STOP_BUTTON_OK_ACTIVE_LEVEL,
             interpreted_di(safety_available, raw_di_level(&safety, safety_available, 5),
                            SAFETY_RELAY_OK_ACTIVE_LEVEL, "ok", "not_ok"),
             SAFETY_RELAY_OK_ACTIVE_LEVEL,
             VFD_FAULT_DI_FITTED
                 ? interpreted_di(safety_available, raw_di_level(&safety, safety_available, 6),
                                  VFD_FAULT_DI_ACTIVE_LEVEL, "fault", "no_fault")
                 : "not_fitted",
             VFD_FAULT_DI_FITTED, VFD_FAULT_DI_ACTIVE_LEVEL,
             CONTACTOR_FEEDBACK_FITTED
                 ? interpreted_di(safety_available, raw_di_level(&safety, safety_available, 7),
                                  CONTACTOR_OK_ACTIVE_LEVEL, "ok", "not_ok")
                 : "not_fitted",
             CONTACTOR_FEEDBACK_FITTED, CONTACTOR_OK_ACTIVE_LEVEL);
    ESP_LOGI("Diag", "sensors initialized=%d sample=%d valid=%d fresh=%d age_ms=%lu "
                     "airspeed=%.3f pressure_pa=%.2f temp_c=%.2f taps_valid=0x%04x errors=%lu",
             !!(ready & EVT_SENSOR_TASK_READY), sample_available,
             sample_available && sample.valid,
             sample_available && sample.valid && ticks_age_ms(sample.tick) < SENSOR_TIMEOUT_MS,
             (unsigned long)(sample_available ? ticks_age_ms(sample.tick) : UINT32_MAX),
             (double)sample.value, (double)sample.pressure_pa, (double)sample.temperature_c,
             sample.taps_valid, (unsigned long)sample.errors);
    ESP_LOGI("Diag", "V20 modbus_initialized=%d status_available=%d drive_ready=%d drive_ok=%d "
                     "actual_hz=%.2f status=0x%04x errors=%lu timeouts=%lu",
             state_available && state.modbus_initialized, motor_available,
             motor.ready, motor.ok, (double)motor.actual_hz, motor.status,
             (unsigned long)motor.errors, (unsigned long)motor.timeouts);
    esp_netif_ip_info_t ip_info = {0};
    const bool ip_info_available = Ethernet_GetIpInfo(&ip_info);
    const bool ethernet_link = Ethernet_LinkUp();
    ESP_LOGI("Diag", "Ethernet link: %s", ethernet_link ? "UP" : "DOWN");
    ESP_LOGI("Diag", "IPv4 configured: %s address=" IPSTR " subnet=" IPSTR " gateway=" IPSTR,
             ip_info_available ? "YES" : "NO", IP2STR(&ip_info.ip),
             IP2STR(&ip_info.netmask), IP2STR(&ip_info.gw));
    ESP_LOGI("Diag", "Pi TCP listener=%d heartbeat_received=%d heartbeat_fresh=%d age_ms=%lu "
                     "(timeout_ms=%u)",
             !!(ready & EVT_PI_TASK_READY),
             heartbeat_available && heartbeat.connected,
             heartbeat_available && heartbeat.connected &&
                 ticks_age_ms(heartbeat.tick) < COMMAND_TIMEOUT_MS,
             (unsigned long)(heartbeat_available ? ticks_age_ms(heartbeat.tick) : UINT32_MAX),
             (unsigned)COMMAND_TIMEOUT_MS);

    DigitalOutputStatus outputs = {0};
    DigitalOutputs_GetStatus(&outputs);
    const StatusIndicator indicator = StatusOutput_Get(&state, state_available,
        (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS));
    ESP_LOGI("Diag", "DO map: DO1/bit0=11K3_VFD_ENABLE DO2/bit1=green DO3/bit2=red "
                     "DO4/bit3=yellow (verify DO2/DO3 lamp colors)");
    ESP_LOGI("Diag", "DO requested=0x%02x acknowledged=0x%02x ack_valid=%d initialized=%d "
                     "11K3_requested=%d write_errors=%lu (I2C ack is not contact feedback)",
             (unsigned)outputs.requested_image, (unsigned)outputs.confirmed_image,
             outputs.confirmed_valid,
             outputs.initialized, outputs.vfd_requested,
             (unsigned long)outputs.write_errors);
    ESP_LOGI("Diag", "status_indicator=%s (onboard RGB only); external status DOs not assigned",
             indicator.text);
}

void app_main(void)
{
    ESP_LOGI("Startup", "app_main entered; reset_reason=%d; free_heap=%lu",
             (int)esp_reset_reason(), (unsigned long)esp_get_free_heap_size());
    StartApplicationTasks();
    ESP_LOGI("Startup", "Application tasks created (not a hardware readiness check)");

    /* Keep diagnostic output available if the terminal opens after boot.
     * No GPIO, output, motor or communication commands are issued here. */
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI("Startup", "Main task alive; free_heap=%lu",
                 (unsigned long)esp_get_free_heap_size());
        LogApplicationStatus();
    }
}
