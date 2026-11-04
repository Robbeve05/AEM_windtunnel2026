/*
 * PiCommunicationTask.c
 * Exchanges commands, heartbeat and telemetry with the Raspberry Pi.
 *
 * Author: R.E. van Eekelen
 */
///////////////////////////////////////////////////////////////////////////////
// Includes

#include "ApplicationTasks.h"
#include "TaskData.h"
#include "AppConfig.h"
#include "PiProtocol.h"
#include "PressureSensors.h"
#include "SafetyIO.h"
#include "StatusOutput.h"
#include "DigitalOutputs.h"
#include "FanCalibration.h"
#include "Ethernet.h"
#include <math.h>
#include "lwip/sockets.h"
#include <errno.h>
#include <unistd.h>
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////
// Local interfaces and connection state

static int client = -1;
static void disconnect_client(void)
{
    if (client >= 0)
    {
        ESP_LOGW("PiCommunication", "TCP client disconnected");
        shutdown(client, SHUT_RDWR);
        close(client);
        client = -1;
    }
    PiHeartbeat heartbeat = {0};
    xQueueOverwrite(pi_heartbeat, &heartbeat);
    xEventGroupSetBits(events, STOP_REQUEST);
    xQueueReset(commands);
}
static void reply(const char *text)
{
    size_t remaining = strlen(text);
    while (remaining && client >= 0)
    {
        int n = send(client, text, remaining, 0);
        if (n <= 0)
        {
            disconnect_client();
            return;
        }
        remaining -= n;
        text += n;
    }
}
static void process(const char *line)
{
#if SIMULATION_MODE
    char name[16], extra;
    int active;
    if (sscanf(line, "SIM %15s %d %c", name, &active, &extra) == 2 && (active == 0 || active == 1))
    {
        reply(SafetyIO_Simulate(name, active != 0) ? "OK SIM\n" : "ERR SIM\n");
        return;
    }
#endif
    if (!strncmp(line,"CAL_",4)) {
        Telemetry t={0}; MotorStatus m={0}; char response[256];
        bool ready=xQueuePeek(telemetry,&t,0)==pdTRUE && t.state==STATE_READY;
        bool stopped=xQueuePeek(motor_status,&m,0)==pdTRUE && m.ready && m.ok &&
            (TickType_t)(xTaskGetTickCount()-m.tick)<pdMS_TO_TICKS(SENSOR_TIMEOUT_MS) && fabsf(m.actual_hz)<=0.2f;
        FanCalibration_HandleCommand(line,ready,stopped,response,sizeof(response));reply(response);return;
    }
    PiCommand c;
    if (!PiProtocol_Parse(line, &c))
    {
        reply("ERR SYNTAX\n");
        return;
    }
    if (c.type == CMD_PING)
    {
        PiHeartbeat heartbeat = {.connected = true, .tick = xTaskGetTickCount()};
        xQueueOverwrite(pi_heartbeat, &heartbeat);
        reply("OK PING\n");
        return;
    }
#if !ALLOW_REMOTE_START
    if (c.type == CMD_START)
    {
        reply("ERR PHYSICAL_START_REQUIRED\n");
        return;
    }
#endif
    if ((c.type == CMD_SETPOINT && (c.values[0] < 0 || c.values[0] > MAX_SETPOINT)) ||
        (c.type == CMD_PID && (c.values[0] < 0 || c.values[0] > 1000 || c.values[1] < 0 ||
                               c.values[1] > 1000 || c.values[2] < 0 || c.values[2] > 1000)))
    {
        reply("ERR RANGE\n");
        return;
    }
    if (c.type == CMD_MODE || c.type == CMD_FREQ) {
        Telemetry current = {0};
        if (xQueuePeek(telemetry, &current, 0) != pdTRUE ||
            (c.type == CMD_MODE && (current.state == STATE_RUNNING || current.state == STATE_STOP)) ||
            (c.type == CMD_FREQ && current.mode != MODE_MANUAL)) {
            reply("ERR MODE_OR_STATE\n"); return;
        }
    }
    if (c.type == CMD_FILTER)
    {
        if (c.values[0] < 0.1f || c.values[0] > 10.0f)
        {
            reply("ERR RANGE\n");
            return;
        }
        SensorLib_SetFilter(c.values[0]);
        reply("OK FILTER\n");
        return;
    }
    if (c.type == CMD_STOP)
    {
        xEventGroupSetBits(events, STOP_REQUEST);
        reply("OK STOP_REQUESTED\n");
        xEventGroupSetBits(handle_SystemEventGroup, EVT_NEW_COMMAND);
    }
    else if (xQueueSend(commands, &c, 0) == pdTRUE)
    {
        xEventGroupSetBits(handle_SystemEventGroup, EVT_NEW_COMMAND);
        reply("OK QUEUED\n");
    }
    else
        reply("ERR BUSY\n");
}

///////////////////////////////////////////////////////////////////////////////
// void PiCommunicationTask(void *argument)
/*
 * Announces readiness after Ethernet and the listening socket initialize.
 * Does not wait for a Pi connection; heartbeat health remains independent.
 */
void PiCommunicationTask(void *argument)
{
    (void)argument;
    Ethernet_Init();
    int server = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    ESP_ERROR_CHECK(server >= 0 ? ESP_OK : ESP_FAIL);
    int reuse = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    struct sockaddr_in address = {.sin_family = AF_INET,
                                  .sin_port = htons(PI_TCP_PORT),
                                  .sin_addr.s_addr = htonl(INADDR_ANY)};
    ESP_ERROR_CHECK(bind(server, (struct sockaddr *)&address, sizeof(address)) == 0 ? ESP_OK
                                                                                    : ESP_FAIL);
    ESP_ERROR_CHECK(listen(server, 1) == 0 ? ESP_OK : ESP_FAIL);
    ESP_LOGI("PiCommunication", "TCP listener ready on port %d", PI_TCP_PORT);
    char line[96];
    size_t used = 0;
    bool overflow = false;
    TickType_t sent = xTaskGetTickCount();
    SystemState_t diagnostic_state = STATE_INIT;
    uint32_t previous_blockers = UINT32_MAX;
    TickType_t last_wait_log = 0;
    xEventGroupSetBits(handle_ThreadEventGroup, EVT_PI_TASK_READY);
    for (;;)
    {
        if (client < 0)
        {
            client = accept(server, NULL, NULL);
            if (client < 0)
            {
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }
            struct timeval timeout = {.tv_usec = 10000};
            setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            timeout.tv_usec = 100000;
            setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
            used = 0;
            overflow = false;
            ESP_LOGI("PiCommunication", "TCP client connected");
            xQueueReset(commands);
            xEventGroupSetBits(events, STOP_REQUEST);
        }
        uint8_t data[64];
        int count = recv(client, data, sizeof(data), 0);
        if (count == 0 || (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK))
        {
            disconnect_client();
            continue;
        }
        for (int i = 0; i < count && client >= 0; ++i)
        {
            char c = (char)data[i];
            if (c == '\n')
            {
                if (overflow)
                    reply("ERR LINE\n");
                else
                {
                    line[used] = '\0';
                    process(line);
                }
                used = 0;
                overflow = false;
            }
            else if (c == '\r')
            { /* CRLF is accepted. */
            }
            else if ((unsigned char)c < 32 || (unsigned char)c > 126)
                overflow = true;
            else if (!overflow && used < sizeof(line) - 1)
                line[used++] = c;
            else
                overflow = true;
        }
        TickType_t now = xTaskGetTickCount();
        if ((TickType_t)(now - sent) >= pdMS_TO_TICKS(TELEMETRY_PERIOD_MS))
        {
            Telemetry t;
            if (xQueuePeek(telemetry, &t, 0) == pdTRUE)
            {
                PiHeartbeat heartbeat = {0};
                const bool heartbeat_received =
                    xQueuePeek(pi_heartbeat, &heartbeat, 0) == pdTRUE && heartbeat.connected;
                const bool heartbeat_fresh =
                    heartbeat_received &&
                    (TickType_t)(now - heartbeat.tick) < pdMS_TO_TICKS(COMMAND_TIMEOUT_MS);
                const bool ethernet_link = Ethernet_LinkUp();
                const bool ipv4_configured = Ethernet_HasIp();
                Sample sample = {0};
                MotorStatus motor = {0};
                xQueuePeek(sensors, &sample, 0);
                xQueuePeek(motor_status, &motor, 0);
                char buffer[3072];
                int length = snprintf(
                    buffer, sizeof(buffer),
                    "STATUS running=%d sensor_ok=%d motor_ok=%d sp=%.3f pv=%.3f out=%.3f reason=%s "
                    "sim=%d sp_ramp=%.3f"
                    " mode=%s manual_hz=%.3f applied_hz=%.3f"
                    " pressure_pa=%.3f temperature_c=%.3f actual_hz=%.3f desired_hz=%.3f "
                    "drive_status=%u drive_fault=%d"
                    " control_hz=%.2f execution_us=%lu max_jitter_us=%lu missed_deadlines=%lu "
                    "overruns=%lu"
                    " sensor_errors=%lu modbus_errors=%lu modbus_timeouts=%lu ethernet=%d "
                    "ethernet_link=%d ipv4_configured=%d pi_tcp_connected=1 "
                    "pi_heartbeat_received=%d pi_heartbeat_fresh=%d "
                    "stack_control=%lu stack_ethernet=%u"
                    " taps_valid=%u filter_hz=%.2f sample_seq=%lu"
                    " state=%s fault=%s last_fault=%s estop=%d relay=%d reset_required=%d stop=%d "
                    "inputs_valid=%d pi=%d commissioned=%d"
                    " estop_pressed=%d safety_relay_ok=%d stop_button_ok=%d pi_connected=%d "
                    "sensor_age_ms=%lu"
                    " raw_di=%lu start=%d reset=%d vfd_fault_available=%d contactor_available=%d "
                    "contactor=%d"
                    " control_period_us=%u service_us=%lu service_max_us=%lu rtt_median_us=%lu "
                    "rtt_p95_us=%lu rtt_max_us=%lu request_id=%lu modbus_hz=%.2f transitions=%lu "
                    "remote_start=%d crc_errors=%lu exceptions=%lu exception_code=%lu "
                    "late_replies=%lu",
                    t.running,
                    (sample.valid &&
                     xTaskGetTickCount() - sample.tick < pdMS_TO_TICKS(SENSOR_TIMEOUT_MS)),
                    t.motor_ok, t.setpoint, sample.value, t.output, t.reason, SIMULATION_MODE,
                    t.control_setpoint, t.mode == MODE_MANUAL ? "MANUAL" : "AUTO",
                    t.manual_hz, t.applied_hz, sample.pressure_pa, sample.temperature_c, motor.actual_hz,
                    t.applied_hz, motor.status, !!(motor.status & 8), t.control_hz,
                    (unsigned long)t.execution_us, (unsigned long)t.max_jitter_us,
                    (unsigned long)t.missed_deadlines, (unsigned long)t.overruns,
                    (unsigned long)sample.errors, (unsigned long)motor.errors,
                    (unsigned long)motor.timeouts, ethernet_link, ethernet_link,
                    ipv4_configured, heartbeat_received, heartbeat_fresh,
                    (unsigned long)t.stack_free,
                    (unsigned)uxTaskGetStackHighWaterMark(NULL), sample.taps_valid,
                    SensorLib_FilterHz(), (unsigned long)sample.sequence,
                    StateMachine_Name(t.state), FaultReason_Name(t.fault),
                    FaultReason_Name(t.last_fault), t.safety.estop_pressed, t.safety.relay_ok,
                    t.reset_required, !t.safety.stop_ok, t.safety.valid, t.pi_ok,
                    t.safety.commissioned, t.safety.estop_pressed, t.safety.relay_ok,
                    t.safety.stop_ok, t.pi_ok,
                    (unsigned long)((xTaskGetTickCount() - sample.tick) * portTICK_PERIOD_MS),
                    (unsigned long)t.safety.raw_levels, t.safety.start_pressed,
                    t.safety.reset_pressed, t.safety.vfd_fault_available,
                    t.safety.contactor_available, t.safety.contactor_ok, CONTROL_PERIOD_US,
                    (unsigned long)t.service_us, (unsigned long)t.service_max_us,
                    (unsigned long)t.rtt_median_us, (unsigned long)t.rtt_p95_us,
                    (unsigned long)t.rtt_max_us, (unsigned long)t.request_id, t.modbus_hz,
                    (unsigned long)t.transitions, ALLOW_REMOTE_START, (unsigned long)t.crc_errors,
                    (unsigned long)t.exceptions, (unsigned long)t.exception_code,
                    (unsigned long)t.late_replies);
                const StatusIndicator indication=StatusOutput_Get(&t,true,
                    (uint32_t)(now*portTICK_PERIOD_MS));
                if(length>0 && length<(int)sizeof(buffer)-256) {
                    length+=snprintf(buffer+length,sizeof(buffer)-length,
                        " led_green=%d led_yellow=%d led_red=%d status_text=",
                        indication.green,indication.amber,indication.red);
                    // Encode spaces so the existing key=value token protocol is unchanged.
                    for(const char *p=indication.text; *p && length<(int)sizeof(buffer)-4; ++p) {
                        if(*p==' ') { memcpy(buffer+length,"%20",3); length+=3; }
                        else buffer[length++]=*p;
                    }
                    buffer[length]=0;
                }
                FanCalibrationStatus cal;FanCalibration_GetStatus(&cal);
                if(length>0 && length<(int)sizeof(buffer)-160)
                    length+=snprintf(buffer+length,sizeof(buffer)-length,
                        " air_density=%.4f cal_valid=%d cal_active=%d cal_points=%u cal_crc=%lu cal_version=%u cal_source=%s",
                        AIR_DENSITY,cal.valid,cal.valid,cal.points,(unsigned long)cal.crc,cal.version,cal.valid?"CALIBRATED":"DEFAULT");
                DigitalOutputStatus outputs;
                DigitalOutputs_GetStatus(&outputs);
                if(length>0 && length<(int)sizeof(buffer)-192)
                    length+=snprintf(buffer+length,sizeof(buffer)-length,
                        " outputs_ok=%d output_write_errors=%lu output_image=%u output_ack_image=%u"
                        " output_ack_valid=%d external_leds_enabled=%d",
                        outputs.initialized && outputs.confirmed_valid,
                        (unsigned long)outputs.write_errors,outputs.requested_image,
                        outputs.confirmed_image,outputs.confirmed_valid,EXTERNAL_STATUS_LEDS_ENABLED);
                const ReadyDiagnostics *diag = &t.ready_diag;
                if (length > 0 && length < (int)sizeof(buffer) - 768)
                    length += snprintf(
                        buffer + length, sizeof(buffer) - length,
                        " diag_blockers=%lu diag_estop_released=%d diag_stop_ok=%d"
                        " diag_reset_detected=%d diag_reset_accepted=%d diag_reset_pending=%d"
                        " diag_safety_inputs_valid=%d diag_safety_inputs_fresh=%d"
                        " diag_safety_commissioned=%d diag_safety_relay_ok=%d"
                        " diag_safety_outputs_healthy=%d"
                        " diag_digital_outputs_initialized=%d diag_digital_outputs_healthy=%d"
                        " diag_modbus_initialized=%d diag_modbus_response_received=%d"
                        " diag_v20_status_word=%u diag_v20_status_healthy=%d diag_v20_ready=%d"
                        " diag_drive_communication_ok=%d diag_modbus_errors=%lu"
                        " diag_modbus_timeouts=%lu diag_v20_run_commissioned=%d"
                        " diag_pitot_communication_ok=%d diag_sensor_data_valid=%d"
                        " diag_sensor_data_fresh=%d diag_sensor_ok=%d"
                        " diag_ethernet_link=%d diag_pi_heartbeat_received=%d"
                        " diag_pi_heartbeat_fresh=%d diag_pi_ok=%d",
                        (unsigned long)diag->blocking_reasons, diag->estop_released, diag->stop_ok,
                        diag->reset_detected, diag->reset_accepted, diag->reset_pending,
                        diag->safety_inputs_valid, diag->safety_inputs_fresh,
                        diag->safety_commissioned, diag->safety_relay_ok,
                        diag->safety_outputs_healthy, outputs.initialized,
                        outputs.initialized && outputs.confirmed_valid, diag->modbus_initialized,
                        diag->modbus_response_received, diag->v20_status_word,
                        diag->v20_status_healthy, diag->v20_ready,
                        diag->drive_communication_ok, (unsigned long)diag->modbus_errors,
                        (unsigned long)diag->modbus_timeouts, diag->v20_run_commissioned,
                        diag->pitot_communication_ok, diag->sensor_data_valid,
                        diag->sensor_data_fresh, diag->sensor_ok, ethernet_link,
                        heartbeat_received, heartbeat_fresh, diag->pi_ok);
                for (int tap = 0; tap < 16 && length > 0 && length < (int)sizeof(buffer) - 32;
                     tap++)
                    length += snprintf(buffer + length, sizeof(buffer) - length, " p%d=%.3f", tap,
                                       (sample.taps_valid & (1u << tap)) ? sample.taps[tap] : 0.0f);
                if (length > 0 && length < (int)sizeof(buffer) - 2)
                {
                    buffer[length++] = '\n';
                    buffer[length] = 0;
                }
                reply(buffer);
                if (t.state == STATE_WAIT &&
                    (diagnostic_state != STATE_WAIT || previous_blockers != diag->blocking_reasons ||
                     (TickType_t)(now - last_wait_log) >=
                         pdMS_TO_TICKS(WAIT_DIAGNOSTICS_INTERVAL_MS)))
                {
                    ESP_LOGI(
                        "WaitDiag",
                        "WAIT READY blocked=0x%08lx state=%s fault=%s reset_pending=%d"
                        " reset_accepted=%d estop_released=%d stop_ok=%d safety_valid=%d"
                        " safety_fresh=%d safety_commissioned=%d relay_ok=%d safety_outputs_ok=%d"
                        " digital_outputs_initialized=%d digital_outputs_healthy=%d"
                        " modbus_initialized=%d modbus_response=%d v20_status=0x%04x"
                        " v20_status_healthy=%d v20_ready=%d drive_comm_ok=%d"
                        " v20_run_commissioned=%d modbus_errors=%lu modbus_timeouts=%lu"
                        " pitot_comm=%d sensor_valid=%d sensor_fresh=%d sensor_ok=%d"
                        " ethernet=%d pi_heartbeat_received=%d pi_heartbeat_fresh=%d pi_ok=%d",
                        (unsigned long)diag->blocking_reasons, StateMachine_Name(t.state),
                        FaultReason_Name(t.last_fault), diag->reset_pending, diag->reset_accepted,
                        diag->estop_released, diag->stop_ok, diag->safety_inputs_valid,
                        diag->safety_inputs_fresh, diag->safety_commissioned,
                        diag->safety_relay_ok, diag->safety_outputs_healthy, outputs.initialized,
                        outputs.initialized && outputs.confirmed_valid, diag->modbus_initialized,
                        diag->modbus_response_received, diag->v20_status_word,
                        diag->v20_status_healthy, diag->v20_ready, diag->drive_communication_ok,
                        diag->v20_run_commissioned, (unsigned long)diag->modbus_errors,
                        (unsigned long)diag->modbus_timeouts, diag->pitot_communication_ok,
                        diag->sensor_data_valid, diag->sensor_data_fresh, diag->sensor_ok,
                        ethernet_link, heartbeat_received, heartbeat_fresh, diag->pi_ok);
                    last_wait_log = now;
                }
                diagnostic_state = t.state;
                previous_blockers = diag->blocking_reasons;
            }
            sent = now;
        }
    }
}
