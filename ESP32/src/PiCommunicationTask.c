///////////////////////////////////////////////////////////////////////////////
// File information

/*
 * PiCommunicationTask.c
 *
 * Exchanges commands, heartbeat and telemetry with the Raspberry Pi.
 *
 * Author: R.E. van Eekelen
 */

///////////////////////////////////////////////////////////////////////////////
// System includes

#include "esp_err.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

///////////////////////////////////////////////////////////////////////////////
// FreeRTOS includes

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

///////////////////////////////////////////////////////////////////////////////
// Application includes

#include "AppConfig.h"
#include "ApplicationTasks.h"
#include "DigitalOutputs.h"
#include "Ethernet.h"
#include "I2CBus.h"
#include "FanCalibration.h"
#include "FaultSnapshot.h"
#include "PiProtocol.h"
#include "PressureSensors.h"
#include "SafetyIO.h"
#include "StatusOutput.h"
#include "TaskData.h"
#include "V20Modbus.h"

///////////////////////////////////////////////////////////////////////////////
// File-local data structures

typedef struct
{
    uint32_t mask;
    const char *name;
} WaitBlockerLabel;

///////////////////////////////////////////////////////////////////////////////
// File-local variables

static int client = -1;
static PiHeartbeat connection;
static const WaitBlockerLabel wait_blocker_labels[] = {
    {WAIT_BLOCK_ESTOP_PRESSED, "estop_pressed"},
    {WAIT_BLOCK_STOP_INPUT, "stop_input"},
    {WAIT_BLOCK_SAFETY_INPUT_INVALID, "safety_input_invalid"},
    {WAIT_BLOCK_SAFETY_INPUT_STALE, "safety_input_stale"},
    {WAIT_BLOCK_SAFETY_NOT_COMMISSIONED, "safety_not_commissioned"},
    {WAIT_BLOCK_SAFETY_RELAY, "safety_relay"},
    {WAIT_BLOCK_SAFETY_OUTPUTS, "safety_outputs"},
    {WAIT_BLOCK_VFD_FAULT, "vfd_fault"},
    {WAIT_BLOCK_CONTACTOR, "contactor_feedback"},
    {WAIT_BLOCK_SENSOR, "sensor"},
    {WAIT_BLOCK_MODBUS_NOT_INITIALIZED, "modbus_not_initialized"},
    {WAIT_BLOCK_MODBUS_NO_RESPONSE, "modbus_no_response"},
    {WAIT_BLOCK_V20_STATUS, "v20_status"},
    {WAIT_BLOCK_DRIVE_COMMUNICATION, "drive_communication"},
    {WAIT_BLOCK_V20_NOT_READY, "v20_not_ready"},
    {WAIT_BLOCK_V20_RUN_LOCK, "v20_run_commissioning_lock"},
    {WAIT_BLOCK_PI, "pi_heartbeat"},
    {WAIT_BLOCK_RESET_SEQUENCE, "physical_reset_not_accepted"},
    {WAIT_BLOCK_RESET_FEEDBACK_PENDING, "reset_feedback_pending"},
    {WAIT_BLOCK_DRIVE_FAULT, "drive_fault"},
    {WAIT_BLOCK_V20_CONFIGURATION, "v20_configuration"},
    {WAIT_BLOCK_V20_OPERATION_INHIBITED, "v20_operation_inhibited"},
};

///////////////////////////////////////////////////////////////////////////////
// Function prototypes

static void format_wait_blockers(uint32_t reasons, char *buffer, size_t size);
static void disconnect_client(void);
static void reply(const char *text);
static void process(const char *line);

///////////////////////////////////////////////////////////////////////////////
// Module-specific functionality

///////////////////////////////////////////////////////////////////////////////
// format_wait_blockers()

static void format_wait_blockers(uint32_t reasons, char *buffer, size_t size)
{
    if (buffer == NULL || size == 0)
        return;
    buffer[0] = '\0';
    size_t used = 0;
    uint32_t known = 0;
    for (size_t i = 0; i < sizeof(wait_blocker_labels) / sizeof(wait_blocker_labels[0]); ++i)
    {
        known |= wait_blocker_labels[i].mask;
        if ((reasons & wait_blocker_labels[i].mask) == 0)
            continue;
        const int written = snprintf(buffer + used, size - used, "%s%s", used == 0 ? "" : ",",
                                     wait_blocker_labels[i].name);
        if (written < 0 || (size_t)written >= size - used)
        {
            buffer[size - 1] = '\0';
            return;
        }
        used += (size_t)written;
    }
    const uint32_t unknown = reasons & ~known;
    if (unknown != 0 && used < size)
    {
        (void)snprintf(buffer + used, size - used, "%sunknown_0x%08lx", used == 0 ? "" : ",",
                       (unsigned long)unknown);
    }
    if (used == 0 && reasons == 0)
        (void)snprintf(buffer, size, "none");
}

///////////////////////////////////////////////////////////////////////////////
// disconnect_client()

static void disconnect_client(void)
{
    if (client >= 0)
    {
        ESP_LOGW("PiCommunication", "TCP client disconnected");
        shutdown(client, SHUT_RDWR);
        close(client);
        client = -1;
    }
    connection.disconnect_sequence++;
    connection.connected = false;
    connection.tcp_connected = false;
    xQueueOverwrite(pi_heartbeat, &connection);
    xEventGroupSetBits(events, STOP_REQUEST);
    xQueueReset(commands);
}

///////////////////////////////////////////////////////////////////////////////
// reply()

static void reply(const char *text)
{
    size_t remaining = strlen(text);
    const TickType_t started = xTaskGetTickCount();
    while (remaining && client >= 0)
    {
        /* Bound the complete reply, including repeated partial sends. */
        if ((TickType_t)(xTaskGetTickCount() - started) >= pdMS_TO_TICKS(100))
        {
            disconnect_client();
            return;
        }
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

///////////////////////////////////////////////////////////////////////////////
// process()

static void process(const char *line)
{
    connection.last_message_tick = xTaskGetTickCount();
    xQueueOverwrite(pi_heartbeat, &connection);
#if SIMULATION_MODE
    char name[32], extra;
    int active;
    if (sscanf(line, "SIM %31s %d %c", name, &active, &extra) == 2 && (active == 0 || active == 1))
    {
        reply((SafetyIO_Simulate(name, active != 0) || V20Modbus_Simulate(name, active != 0)) ? "OK SIM\n" : "ERR SIM\n");
        return;
    }
#endif
    if (!strncmp(line, "CAL_", 4))
    {
        Telemetry t = {0};
        MotorStatus m = {0};
        char response[256];
        bool ready = xQueuePeek(telemetry, &t, 0) == pdTRUE && t.state == STATE_READY;
        bool stopped =
            xQueuePeek(motor_status, &m, 0) == pdTRUE && m.ready && m.ok &&
            (TickType_t)(xTaskGetTickCount() - m.tick) < pdMS_TO_TICKS(SENSOR_TIMEOUT_MS) &&
            fabsf(m.actual_hz) <= 0.2f;
        FanCalibration_HandleCommand(line, ready, stopped, response, sizeof(response));
        reply(response);
        return;
    }
    PiCommand c;
    if (!PiProtocol_Parse(line, &c))
    {
        reply("ERR SYNTAX\n");
        return;
    }
    if (c.type == CMD_PING)
    {
        connection.connected = true;
        connection.tick = xTaskGetTickCount();
        xQueueOverwrite(pi_heartbeat, &connection);
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
    if (c.type == CMD_MODE || c.type == CMD_FREQ)
    {
        Telemetry current = {0};
        if (xQueuePeek(telemetry, &current, 0) != pdTRUE ||
            (c.type == CMD_MODE &&
             (current.state == STATE_STARTING || current.state == STATE_RUNNING || current.state == STATE_STOP)) ||
            (c.type == CMD_FREQ && current.mode != MODE_MANUAL))
        {
            reply("ERR MODE_OR_STATE\n");
            return;
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
// PiCommunicationTask()

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
    uint32_t sent_fault_sequence = 0;
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
            sent_fault_sequence = 0;
            connection = (PiHeartbeat){.tcp_connected = true,
                .disconnect_sequence = connection.disconnect_sequence};
            xQueueOverwrite(pi_heartbeat, &connection);
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
                    (TickType_t)(xTaskGetTickCount() - heartbeat.tick) < pdMS_TO_TICKS(COMMAND_TIMEOUT_MS);
                const bool ethernet_link = Ethernet_LinkUp();
                const bool ipv4_configured = Ethernet_HasIp();
                Sample sample = {0};
                MotorStatus motor = {0};
                xQueuePeek(sensors, &sample, 0);
                xQueuePeek(motor_status, &motor, 0);
                /* Single task owns this buffer. Keep full diagnostics off the
                 * task stack; the old 1024-byte buffer could never append them. */
                static char buffer[12288];
                int length = snprintf(
                    buffer, sizeof(buffer),
                    "STATUS running=%d sensor_ok=%d motor_ok=%d sp=%.3f pv=%.3f "
                    "out=%.3f reason=%s "
                    "sim=%d sp_ramp=%.3f"
                    " mode=%s manual_hz=%.3f applied_hz=%.3f"
                    " pressure_pa=%.3f temperature_c=%.3f actual_hz=%.3f "
                    "desired_hz=%.3f "
                    "drive_status=%u drive_fault=%d"
                    " control_hz=%.2f execution_us=%lu max_jitter_us=%lu "
                    "missed_deadlines=%lu "
                    "overruns=%lu"
                    " sensor_errors=%lu modbus_errors=%lu modbus_timeouts=%lu "
                    "ethernet=%d "
                    "ethernet_link=%d ipv4_configured=%d pi_tcp_connected=1 "
                    "pi_heartbeat_received=%d pi_heartbeat_fresh=%d "
                    "stack_control=%lu stack_ethernet=%u"
                    " taps_valid=%u filter_hz=%.2f sample_seq=%lu"
                    " state=%s fault=%s last_fault=%s estop=%d relay=%d "
                    "reset_required=%d stop=%d "
                    "inputs_valid=%d pi=%d commissioned=%d"
                    " estop_pressed=%d safety_relay_ok=%d stop_button_ok=%d "
                    "pi_connected=%d "
                    "sensor_age_ms=%lu"
                    " raw_di=%lu start=%d reset=%d vfd_fault_available=%d "
                    "contactor_available=%d "
                    "contactor=%d"
                    " control_period_us=%u service_us=%lu service_max_us=%lu "
                    "rtt_median_us=%lu "
                    "rtt_p95_us=%lu rtt_max_us=%lu request_id=%lu modbus_hz=%.2f "
                    "transitions=%lu "
                    "remote_start=%d crc_errors=%lu exceptions=%lu exception_code=%lu "
                    "late_replies=%lu",
                    t.running,
                    (sample.valid &&
                     xTaskGetTickCount() - sample.tick < pdMS_TO_TICKS(SENSOR_TIMEOUT_MS)),
                    t.motor_ok, t.setpoint, sample.value, t.output, t.reason, SIMULATION_MODE,
                    t.control_setpoint, t.mode == MODE_MANUAL ? "MANUAL" : "AUTO", t.manual_hz,
                    t.applied_hz, sample.pressure_pa, sample.temperature_c, motor.actual_hz,
                    t.applied_hz, motor.status, !!(motor.status & 8), t.control_hz,
                    (unsigned long)t.execution_us, (unsigned long)t.max_jitter_us,
                    (unsigned long)t.missed_deadlines, (unsigned long)t.overruns,
                    (unsigned long)sample.errors, (unsigned long)motor.errors,
                    (unsigned long)motor.timeouts, ethernet_link, ethernet_link, ipv4_configured,
                    heartbeat_received, heartbeat_fresh, (unsigned long)t.stack_free,
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
                PitotDiagnostics pitot;
                SensorLib_GetPitotDiagnostics(&pitot);
                if (length > 0 && length < (int)sizeof(buffer) - 768)
                    length += snprintf(buffer + length, sizeof(buffer) - length,
                        " pitot_raw_count=%u pitot_status_bits=%u pitot_raw_pressure_pa=%.3f pitot_zero_offset_pa=%.3f pitot_corrected_pressure_pa=%.3f pitot_filtered_pressure_pa=%.3f pitot_pressure_pa=%.3f pitot_airspeed_ms=%.3f pitot_zero_samples=%lu pitot_calibrated=%lu pitot_sample_seq=%lu pitot_read_ok=%lu pitot_valid=%lu pitot_age_ms=%lu",
                        (unsigned)pitot.pitot_raw_count,
                        (unsigned)pitot.pitot_status_bits,
                        pitot.pitot_raw_pressure_pa,
                        pitot.pitot_zero_offset_pa,
                        pitot.pitot_corrected_pressure_pa,
                        pitot.pitot_filtered_pressure_pa,
                        pitot.pitot_pressure_pa,
                        pitot.pitot_airspeed_ms,
                        (unsigned long)pitot.pitot_zero_samples,
                        (unsigned long)pitot.pitot_calibrated,
                        (unsigned long)pitot.pitot_sample_seq,
                        (unsigned long)pitot.pitot_read_ok,
                        (unsigned long)pitot.pitot_valid,
                        (unsigned long)((xTaskGetTickCount() - pitot.pitot_tick) * portTICK_PERIOD_MS));
                if (length > 0 && length < (int)sizeof(buffer))
                {
                    const int added = I2CBus_FormatDiagnostics(buffer + length, sizeof(buffer) - (size_t)length);
                    if (added < 0) continue; /* Never transmit a truncated STATUS. */
                    length += added;
                }
                const StatusIndicator indication =
                    StatusOutput_Get(&t, true, (uint32_t)(now * portTICK_PERIOD_MS));
                if (length > 0 && length < (int)sizeof(buffer) - 256)
                {
                    length += snprintf(buffer + length, sizeof(buffer) - length,
                                       " led_green=%d led_yellow=%d led_red=%d status_text=",
                                       indication.green, indication.amber, indication.red);
                    // Encode spaces so the existing key=value token protocol is
                    // unchanged.
                    for (const char *p = indication.text; *p && length < (int)sizeof(buffer) - 4;
                         ++p)
                    {
                        if (*p == ' ')
                        {
                            memcpy(buffer + length, "%20", 3);
                            length += 3;
                        }
                        else
                            buffer[length++] = *p;
                    }
                    buffer[length] = 0;
                }
                DigitalOutputStatus outputs;
                DigitalOutputs_GetStatus(&outputs);
                FanCalibrationStatus cal;
                FanCalibration_GetStatus(&cal);
                if (length > 0 && length < (int)sizeof(buffer) - 256)
                    length += snprintf(buffer + length, sizeof(buffer) - length,
                        " output_write_ack_valid=%d output_latch_valid=%d output_latch_image=%u"
                        " output_latch_match=%d output_read_errors=%lu output_latch_mismatches=%lu",
                        outputs.write_ack_valid, outputs.latch_valid, outputs.latch_image,
                        outputs.latch_valid && outputs.latch_image == outputs.requested_image,
                        (unsigned long)outputs.read_errors, (unsigned long)outputs.latch_mismatches);
                if (length > 0 && length < (int)sizeof(buffer) - 160)
                    length += snprintf(buffer + length, sizeof(buffer) - length,
                                       " air_density=%.4f cal_valid=%d cal_active=%d "
                                       "cal_points=%u cal_crc=%lu cal_version=%u cal_source=%s",
                                       AIR_DENSITY, cal.valid, cal.valid, cal.points,
                                       (unsigned long)cal.crc, cal.version,
                                       cal.valid ? "CALIBRATED" : "DEFAULT");
                if (length > 0 && length < (int)sizeof(buffer) - 192)
                    length += snprintf(buffer + length, sizeof(buffer) - length,
                                       " outputs_ok=%d output_write_errors=%lu output_image=%u "
                                       "output_ack_image=%u"
                                       " output_ack_valid=%d external_leds_enabled=%d",
                                       outputs.initialized && outputs.confirmed_valid,
                                       (unsigned long)outputs.write_errors, outputs.requested_image,
                                       outputs.confirmed_image, outputs.confirmed_valid,
                                       EXTERNAL_STATUS_LEDS_ENABLED);
                if (length > 0 && length < (int)sizeof(buffer) - 160)
                    length += snprintf(buffer + length, sizeof(buffer) - length,
                        " first_fault_valid=%d first_fault_sequence=%lu"
                        " fault_do1_off_observed_ms=%lu fault_off2_observed_ms=%lu",
                        t.first_fault.valid, (unsigned long)t.first_fault.sequence,
                        (unsigned long)t.fault_do1_off_observed_ms,
                        (unsigned long)t.fault_off2_observed_ms);
                if (length > 0 && length < (int)sizeof(buffer) - 448)
                    length += snprintf(buffer + length, sizeof(buffer) - length,
                        " modbus_last_valid_response_ms=%lu modbus_last_valid_process_data_ms=%lu"
                        " modbus_max_process_data_gap_ms=%lu modbus_max_process_data_tx_gap_ms=%lu"
                        " modbus_max_response_gap_ms=%lu modbus_valid_reads=%lu modbus_valid_writes=%lu"
                        " modbus_crc_errors=%lu modbus_invalid_frames=%lu modbus_late_replies=%lu modbus_exceptions=%lu",
                        (unsigned long)motor.last_valid_response_ms,
                        (unsigned long)motor.last_valid_process_data_ms,
                        (unsigned long)motor.max_process_data_gap_ms,
                        (unsigned long)motor.max_process_data_tx_gap_ms,
                        (unsigned long)motor.max_response_gap_ms,
                        (unsigned long)motor.valid_reads, (unsigned long)motor.valid_writes,
                        (unsigned long)motor.crc_errors, (unsigned long)motor.invalid_frames,
                        (unsigned long)motor.late_replies, (unsigned long)motor.exceptions);
                if (length > 0 && length < (int)sizeof(buffer) - 240)
                    length += snprintf(buffer + length, sizeof(buffer) - length,
                        " configuration_failure_reason=%s configuration_start_ready=%d control_type=%u",
                        t.configuration.failure_reason ? t.configuration.failure_reason : "NONE",
                        t.configuration.start_ready, t.configuration.control_type);
                if (length > 0 && length < (int)sizeof(buffer) - 256)
                    length += snprintf(buffer + length, sizeof(buffer) - length,
                        " modbus_transaction_id=%lu modbus_transaction_pending=%d modbus_rtt_us=%lu"
                        " modbus_rtt_max_us=%lu modbus_enqueue_us=%lu modbus_rx_bytes=%u modbus_expected_rx_bytes=%u",
                        (unsigned long)motor.transaction.id, motor.transaction.pending,
                        (unsigned long)motor.transaction.rtt_us, (unsigned long)motor.transaction.max_rtt_us,
                        (unsigned long)motor.transaction.enqueue_us, motor.transaction.rx_bytes,
                        motor.transaction.expected_rx_bytes);
                if (length > 0 && length < (int)sizeof(buffer) - 1024)
                    length += snprintf(buffer + length, sizeof(buffer) - length,
                        " modbus_wire_load_permille=%lu modbus_transactions_per_minute=%lu"
                        " modbus_write_cycle_us=%lu modbus_status_cycle_us=%lu"
                        " modbus_status_cycle_max_us=%lu modbus_write_jitter_max_us=%lu"
                        " modbus_write_deadline_misses=%lu modbus_transaction_max_us=%lu"
                        " modbus_idle_us=%lu modbus_idle_min_us=%lu modbus_idle_max_us=%lu"
                        " modbus_service_gap_max_us=%lu modbus_retries=%lu"
                        " modbus_recovered_transactions=%lu modbus_final_comm_errors=%lu",
                        (unsigned long)motor.bus.wire_load_permille, (unsigned long)motor.bus.transactions_per_minute,
                        (unsigned long)motor.bus.write_cycle_us, (unsigned long)motor.bus.status_cycle_us,
                        (unsigned long)motor.bus.max_status_cycle_us, (unsigned long)motor.bus.max_write_jitter_us,
                        (unsigned long)motor.bus.write_deadline_misses, (unsigned long)motor.bus.max_transaction_us,
                        (unsigned long)motor.bus.last_idle_us, (unsigned long)motor.bus.min_idle_us,
                        (unsigned long)motor.bus.max_idle_us, (unsigned long)motor.bus.max_service_gap_us,
                        (unsigned long)motor.retries, (unsigned long)motor.recovered_transactions,
                        (unsigned long)motor.final_comm_errors);
                const ReadyDiagnostics *diag = &t.ready_diag;
                if (length > 0 && length < (int)sizeof(buffer) - 3072)
                    length += snprintf(
                        buffer + length, sizeof(buffer) - length,
                        " diag_blockers=%lu diag_estop_released=%d diag_stop_ok=%d"
                        " diag_reset_detected=%d diag_reset_accepted=%d "
                        "diag_reset_pending=%d"
                        " diag_safety_inputs_valid=%d diag_safety_inputs_fresh=%d"
                        " diag_safety_commissioned=%d diag_safety_relay_ok=%d"
                        " diag_safety_outputs_healthy=%d"
                        " diag_digital_outputs_initialized=%d "
                        "diag_digital_outputs_healthy=%d"
                        " diag_do1_requested=%d diag_do1_acknowledged=%d"
                        " diag_do1_ack_valid=%d diag_11k3_feedback_ok=%d"
                        " diag_modbus_initialized=%d diag_modbus_response_received=%d"
                        " diag_v20_status_word=%u diag_v20_status_healthy=%d "
                        "diag_v20_ready=%d"
                        " diag_v20_ready_to_switch_on=%d diag_v20_ready_to_operate=%d"
                        " diag_v20_operation_enabled=%d"
                        " diag_v20_off2_active=%d diag_v20_off3_active=%d"
                        " diag_v20_switch_on_inhibited=%d"
                        " diag_v20_control_word_requested=%u"
                        " diag_v20_control_word_requested_valid=%d"
                        " diag_v20_control_word_acknowledged=%u"
                        " diag_v20_control_word_acknowledged_valid=%d"
                        " diag_drive_communication_ok=%d diag_modbus_errors=%lu"
                        " diag_modbus_timeouts=%lu diag_v20_run_commissioned=%d"
                        " diag_drive_fault_free=%d diag_operational_ready=%d "
                        "diag_run_authorized=%d"
                        " diag_v20_diagnostics_complete=%d diag_v20_reference_valid=%d"
                        " diag_v20_reference_config_valid=%d diag_v20_reference_value=%u"
                        " diag_v20_watchdog_valid=%d diag_v20_watchdog_config_known=%d"
                        " diag_v20_watchdog_config_valid=%d "
                        "diag_v20_watchdog_register_value=%u"
                        " diag_v20_watchdog_parameter_expected_value=%u"
                        " diag_v20_last_error=%u diag_v20_last_operation=%u"
                        " diag_v20_last_error_register=%u diag_v20_last_error_expected=%u"
                        " diag_v20_last_error_actual=%u diag_v20_exception_code=%lu"
                        " diag_v20_last_success_age_ms=%lu init_deadline_exceeded=%d"
                        " diag_pitot_communication_ok=%d diag_sensor_data_valid=%d"
                        " diag_sensor_data_fresh=%d diag_sensor_ok=%d"
                        " diag_ethernet_link=%d diag_pi_heartbeat_received=%d"
                        " diag_pi_heartbeat_fresh=%d diag_pi_ok=%d",
                        (unsigned long)diag->blocking_reasons, diag->estop_released, diag->stop_ok,
                        diag->reset_detected, diag->reset_accepted, diag->reset_pending,
                        diag->safety_inputs_valid, diag->safety_inputs_fresh,
                        diag->safety_commissioned, diag->safety_relay_ok,
                        diag->safety_outputs_healthy, outputs.initialized,
                        outputs.initialized && outputs.confirmed_valid, diag->do1_requested,
                        diag->do1_acknowledged, diag->do1_ack_valid, diag->relay_feedback_ok,
                        diag->modbus_initialized, diag->modbus_response_received,
                        diag->v20_status_word, diag->v20_status_healthy, diag->v20_ready,
                        diag->v20_ready_to_switch_on, diag->v20_ready_to_operate,
                        diag->v20_operation_enabled, diag->v20_off2_active, diag->v20_off3_active,
                        diag->v20_switch_on_inhibited, diag->v20_control_word_requested,
                        diag->v20_control_word_requested_valid, diag->v20_control_word_acknowledged,
                        diag->v20_control_word_acknowledged_valid, diag->drive_communication_ok,
                        (unsigned long)diag->modbus_errors, (unsigned long)diag->modbus_timeouts,
                        diag->v20_run_commissioned, diag->drive_fault_free, diag->operational_ready,
                        diag->run_authorized, diag->diagnostics_complete, diag->reference_valid,
                        diag->reference_config_valid, diag->reference_value, diag->watchdog_valid,
                        diag->watchdog_config_known, diag->watchdog_config_valid,
                        diag->watchdog_register_value, diag->watchdog_parameter_expected_value,
                        diag->modbus_last_error, diag->modbus_last_operation,
                        diag->modbus_last_error_register, diag->modbus_last_error_expected,
                        diag->modbus_last_error_actual, (unsigned long)diag->modbus_exception_code,
                        (unsigned long)diag->modbus_last_success_age_ms, t.init_deadline_exceeded,
                        diag->pitot_communication_ok, diag->sensor_data_valid,
                        diag->sensor_data_fresh, diag->sensor_ok, ethernet_link, heartbeat_received,
                        heartbeat_fresh, diag->pi_ok);
                if (length > 0 && length < (int)sizeof(buffer) - 180)
                    length += snprintf(buffer + length, sizeof(buffer) - length,
                        " diag_v20_run_locked=%d diag_v20_warning=%d diag_v20_fault_ack_pending=%d"
                        " diag_v20_stop_attempts=%lu",
                        motor.run_locked, motor.warning_active, motor.fault_ack_pending,
                        (unsigned long)motor.stop_attempts);
                for (int tap = 0; tap < 16 && length > 0 && length < (int)sizeof(buffer) - 32;
                     tap++)
                    length += snprintf(buffer + length, sizeof(buffer) - length, " p%d=%.3f", tap,
                                       (sample.taps_valid & (1u << tap)) ? sample.taps[tap] : 0.0f);
                if (length <= 0 || length >= (int)sizeof(buffer) - 2)
                {
                    ESP_LOGE("PiCommunication", "STATUS frame exceeded buffer; frame discarded");
                    continue;
                }
                if (length > 0 && length < (int)sizeof(buffer) - 2)
                {
                    buffer[length++] = '\n';
                    buffer[length] = 0;
                }
                reply(buffer);
                if (client >= 0 && t.first_fault.valid &&
                    t.first_fault.sequence != sent_fault_sequence)
                {
                    if (FaultSnapshot_Format(&t.first_fault, buffer, sizeof(buffer)))
                    {
                        reply(buffer);
                        if (client >= 0) sent_fault_sequence = t.first_fault.sequence;
                    }
                }
                if (t.state == STATE_WAIT && (diagnostic_state != STATE_WAIT ||
                                              previous_blockers != diag->blocking_reasons ||
                                              (TickType_t)(now - last_wait_log) >=
                                                  pdMS_TO_TICKS(WAIT_DIAGNOSTICS_INTERVAL_MS)))
                {
                    char blocker_names[256];
                    format_wait_blockers(diag->blocking_reasons, blocker_names,
                                         sizeof(blocker_names));
                    ESP_LOGI(
                        "WaitDiag",
                        "WAIT READY blocked=0x%08lx blockers=%s state=%s fault=%s "
                        "reset_pending=%d"
                        " reset_accepted=%d estop_released=%d stop_ok=%d safety_valid=%d"
                        " safety_fresh=%d safety_commissioned=%d relay_ok=%d "
                        "safety_outputs_ok=%d"
                        " digital_outputs_initialized=%d digital_outputs_healthy=%d"
                        " modbus_initialized=%d modbus_response=%d v20_status=0x%04x"
                        " v20_status_healthy=%d v20_ready=%d drive_comm_ok=%d"
                        " drive_fault_free=%d ready_to_switch_on=%d ready_to_operate=%d"
                        " operation_enabled=%d"
                        " OFF2_active=%d OFF3_active=%d switch_on_inhibited=%d"
                        " control_word_requested=0x%04x requested_valid=%d"
                        " control_word_acknowledged=0x%04x acknowledged_valid=%d"
                        " operational_ready=%d run_authorized=%d"
                        " v20_run_commissioned=%d diagnostics_complete=%d"
                        " reference_valid=%d reference_config_valid=%d reference_value=%u"
                        " watchdog_valid=%d watchdog_config_known=%d "
                        "watchdog_config_valid=%d"
                        " watchdog_register=%u watchdog_parameter_expected_ms=%u"
                        " last_error=%u last_operation=%u last_error_register=%u"
                        " last_error_expected=%u last_error_actual=%u exception_code=%lu"
                        " last_response_age_ms=%lu init_deadline_exceeded=%d"
                        " do1_requested=%d do1_acknowledged=%d do1_ack_valid=%d"
                        " relay_feedback_ok=%d modbus_errors=%lu modbus_timeouts=%lu"
                        " pitot_comm=%d sensor_valid=%d sensor_fresh=%d sensor_ok=%d"
                        " ethernet=%d pi_heartbeat_received=%d pi_heartbeat_fresh=%d "
                        "pi_ok=%d",
                        (unsigned long)diag->blocking_reasons, blocker_names,
                        StateMachine_Name(t.state), FaultReason_Name(t.last_fault),
                        diag->reset_pending, diag->reset_accepted, diag->estop_released,
                        diag->stop_ok, diag->safety_inputs_valid, diag->safety_inputs_fresh,
                        diag->safety_commissioned, diag->safety_relay_ok,
                        diag->safety_outputs_healthy, outputs.initialized,
                        outputs.initialized && outputs.confirmed_valid, diag->modbus_initialized,
                        diag->modbus_response_received, diag->v20_status_word,
                        diag->v20_status_healthy, diag->v20_ready, diag->drive_communication_ok,
                        diag->drive_fault_free, diag->v20_ready_to_switch_on,
                        diag->v20_ready_to_operate, diag->v20_operation_enabled,
                        diag->v20_off2_active, diag->v20_off3_active, diag->v20_switch_on_inhibited,
                        diag->v20_control_word_requested, diag->v20_control_word_requested_valid,
                        diag->v20_control_word_acknowledged,
                        diag->v20_control_word_acknowledged_valid, diag->operational_ready,
                        diag->run_authorized, diag->v20_run_commissioned,
                        diag->diagnostics_complete, diag->reference_valid,
                        diag->reference_config_valid, diag->reference_value, diag->watchdog_valid,
                        diag->watchdog_config_known, diag->watchdog_config_valid,
                        diag->watchdog_register_value, diag->watchdog_parameter_expected_value,
                        diag->modbus_last_error, diag->modbus_last_operation,
                        diag->modbus_last_error_register, diag->modbus_last_error_expected,
                        diag->modbus_last_error_actual, (unsigned long)diag->modbus_exception_code,
                        (unsigned long)diag->modbus_last_success_age_ms, t.init_deadline_exceeded,
                        diag->do1_requested, diag->do1_acknowledged, diag->do1_ack_valid,
                        diag->relay_feedback_ok, (unsigned long)diag->modbus_errors,
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
