/*
 * V20Modbus.c
 *
 * Siemens SINAMICS V20: Modbus RTU protocol + UART/RS-485 transport.
 * This entire module is serviced within ControlTask (Core 1, 5 ms).
 * No vTaskDelay(), blocking UART receive, or actuator FreeRTOS task.
 *
 * IMPORTANT: This is an operational motor interface, NOT a safety function.
 * A separately validated hardware safety circuit must inhibit motor torque
 * and allow the fan to coast on E-stop even if this MCU stops running.
 *
 * Author: R.E. van Eekelen
 */
#include "V20Modbus.h"
#include "AppConfig.h"

#include <math.h>
#include <string.h>
#include "esp_timer.h"
#include "driver/uart.h"

_Static_assert(V20_EXPECTED_REFERENCE_CENTIHZ == 5000, "V20 reference must remain 50 Hz");
_Static_assert(V20_REFERENCE_HZ == 50.0f && MANUAL_MAX_HZ <= 50.0f, "50 Hz command ceiling required");

#define V20_PDU_STW_HSW        99u   /* Holding registers 40100 and 40101. */
#define V20_PDU_ZSW_HIW       109u   /* Holding registers 40110 and 40111. */
#define V20_PDU_WATCHDOG     0u     /* Holding register 40001. */
#define V20_PDU_REF_FREQ     15u    /* Holding register 40016. */
#define V20_WORD_OFF1        0x047Eu
#define V20_WORD_RUN         0x047Fu
#define V20_FC_READ          3u
#define V20_FC_WRITE_MULTI   16u
#define V20_MAX_FRAME        32u
#define V20_MAX_REQUEST_AGE_MS 50u
#define US_PER_MS            1000LL
#define V20_TX_BUFFER        256
#define V20_RX_BUFFER        256

/* The RTU inter-frame silence is fixed at 1.75 ms above 19.2 kbaud. */
#if MODBUS_BAUD > 19200
#define V20_FRAME_GAP_US 1750LL
#else
#define V20_FRAME_GAP_US ((38500000LL + MODBUS_BAUD - 1) / MODBUS_BAUD)
#endif

typedef enum {
    OP_NONE = 0,
    OP_BOOT_STOP,
    OP_READ_WATCHDOG,
    OP_READ_REFERENCE,
    OP_READ_STATUS,
    OP_MOTOR_COMMAND
} Operation;

typedef enum {
    PH_IDLE = 0,
    PH_WAIT_RESPONSE,
    PH_RECOVERY
} Phase;

typedef struct {
    bool uart_ok;
    bool configured;
    bool armed;
    bool latched_fault;
    bool status_valid;
    bool drive_ready;
    bool pending_stop;
    bool recheck_pending;
    bool last_run_command;
    bool demand_run;
    Operation bootstrap_next;
    Operation active_operation;
    Phase phase;
    uint16_t status_word;
    float actual_hz;
    uint32_t errors;
    uint32_t timeouts;
    int64_t last_rx_success_us;
    int64_t last_status_us;
    int64_t last_command_us;
    int64_t tx_started_us;
    int64_t bus_quiet_until_us;
    uint8_t tx[V20_MAX_FRAME];
    uint8_t rx[V20_MAX_FRAME];
    size_t rx_used;
    size_t rx_expected;
    uint16_t active_address;
    uint16_t active_count;
    uint16_t active_stw;
    uint16_t active_hsw;
} V20Context;

static V20Context d;

uint16_t V20Modbus_Crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFu;
    if (data == NULL) return 0;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (unsigned j = 0; j < 8; ++j) {
            crc = (uint16_t)((crc >> 1) ^ ((crc & 1u) ? 0xA001u : 0u));
        }
    }
    return crc;
}

static void put_word(uint8_t *dst, uint16_t word)
{
    dst[0] = (uint8_t)(word >> 8);
    dst[1] = (uint8_t)word;
}

static uint16_t get_word(const uint8_t *src)
{
    return (uint16_t)(((uint16_t)src[0] << 8) | src[1]);
}

static size_t seal_frame(uint8_t *frame, size_t payload_size)
{
    const uint16_t crc = V20Modbus_Crc16(frame, payload_size);
    frame[payload_size] = (uint8_t)crc;
    frame[payload_size + 1] = (uint8_t)(crc >> 8);
    return payload_size + 2;
}

static size_t build_read(uint8_t *dst, uint16_t address, uint16_t count)
{
    dst[0] = MODBUS_SLAVE_ADDRESS;
    dst[1] = V20_FC_READ;
    put_word(&dst[2], address);
    put_word(&dst[4], count);
    return seal_frame(dst, 6);
}

static size_t build_command(uint8_t *dst, uint16_t stw, uint16_t hsw)
{
    dst[0] = MODBUS_SLAVE_ADDRESS;
    dst[1] = V20_FC_WRITE_MULTI;
    put_word(&dst[2], V20_PDU_STW_HSW);
    put_word(&dst[4], 2);
    dst[6] = 4;
    put_word(&dst[7], stw);
    put_word(&dst[9], hsw);
    return seal_frame(dst, 11);
}

static bool status_word_healthy(uint16_t word)
{
    return (word & 0x0201u) == 0x0201u && (word & 0x0048u) == 0;
}

/* Enter a software lockout; hardware safety is independent of this action. */
static void latch_communication_fault(int64_t now_us, bool timeout)
{
    d.errors++;
    if (timeout) d.timeouts++;
    d.armed = false;
    d.latched_fault = true;
    d.drive_ready = false;
    d.pending_stop = true;
    d.configured = false;
    d.phase = PH_RECOVERY;
    d.rx_used = 0;
    d.active_operation = OP_NONE;
    d.bus_quiet_until_us = now_us + (int64_t)V20_RESPONSE_TIMEOUT_MS * 2 * US_PER_MS;
}

/*
 * One bounded queue operation. TX buffering prevents waiting for the entire
 * frame to leave the UART, while the control loop only handles completed RX.
 * Only this module accesses the UART; no second task may share the port.
 */
static bool send_operation(Operation operation, bool run, float percent, int64_t now_us)
{
    if (d.phase != PH_IDLE || now_us < d.bus_quiet_until_us) return false;

    size_t size = 0;
    d.active_operation = operation;
    if (operation == OP_BOOT_STOP || operation == OP_MOTOR_COMMAND) {
        uint16_t stw = run ? V20_WORD_RUN : V20_WORD_OFF1;
        uint16_t hsw = run ? (uint16_t)lroundf(percent * 16384.0f / 100.0f) : 0;
        size = build_command(d.tx, stw, hsw);
        d.active_stw = stw;
        d.active_hsw = hsw;
        d.active_address = V20_PDU_STW_HSW;
        d.active_count = 2;
        d.rx_expected = 8;
    } else {
        const uint16_t address = operation == OP_READ_WATCHDOG ? V20_PDU_WATCHDOG :
            operation == OP_READ_REFERENCE ? V20_PDU_REF_FREQ : V20_PDU_ZSW_HIW;
        const uint16_t count = operation == OP_READ_STATUS ? 2 : 1;
        size = build_read(d.tx, address, count);
        d.active_address = address;
        d.active_count = count;
        d.rx_expected = 5u + 2u * count;
    }

#if SIMULATION_MODE
    (void)size;
    /* The simulator completes this operation on a later Service() call. */
#else
    /* Flush *before* a new transaction, never in the middle of a reply. */
    if (uart_flush_input(MODBUS_UART_PORT) != ESP_OK ||
        uart_write_bytes(MODBUS_UART_PORT, (const char *)d.tx, size) != (int)size) {
        latch_communication_fault(now_us, false);
        return false;
    }
#endif
    d.rx_used = 0;
    d.tx_started_us = now_us;
    d.phase = PH_WAIT_RESPONSE;
    return true;
}

static bool verify_reply(void)
{
    const uint8_t *reply = d.rx;
    const size_t size = d.rx_used;
    if (size != d.rx_expected || size < 5 || reply[0] != MODBUS_SLAVE_ADDRESS)
        return false;
    const uint16_t crc = V20Modbus_Crc16(reply, size - 2);
    if (reply[size - 2] != (uint8_t)crc || reply[size - 1] != (uint8_t)(crc >> 8))
        return false;
    if (reply[1] == (d.tx[1] | 0x80u)) return false; /* Modbus exception. */
    if (reply[1] != d.tx[1]) return false;
    if (reply[1] == V20_FC_WRITE_MULTI) {
        return get_word(&reply[2]) == d.active_address &&
            get_word(&reply[4]) == d.active_count;
    }
    return reply[2] == 2u * d.active_count;
}

static void accept_reply(int64_t now_us)
{
    d.last_rx_success_us = now_us;
    d.bus_quiet_until_us = now_us + V20_FRAME_GAP_US;
    d.phase = PH_IDLE;

    switch (d.active_operation) {
    case OP_BOOT_STOP:
        d.last_run_command = false;
        d.pending_stop = false;
        d.bootstrap_next = OP_READ_WATCHDOG;
        break;
    case OP_READ_WATCHDOG:
        if (get_word(&d.rx[3]) != V20_EXPECTED_WATCHDOG_MS) {
            latch_communication_fault(now_us, false);
            return;
        }
        d.bootstrap_next = OP_READ_REFERENCE;
        break;
    case OP_READ_REFERENCE:
        if (get_word(&d.rx[3]) != V20_EXPECTED_REFERENCE_CENTIHZ) {
            latch_communication_fault(now_us, false);
            return;
        }
        d.configured = true;
        d.bootstrap_next = OP_NONE;
        break;
    case OP_READ_STATUS: {
        d.status_word = get_word(&d.rx[3]);
        const uint16_t raw = get_word(&d.rx[5]);
        const int32_t signed_raw = raw < 32768u ? (int32_t)raw : (int32_t)raw - 65536;
        d.actual_hz = (float)signed_raw * V20_REFERENCE_HZ / 16384.0f;
        d.drive_ready = status_word_healthy(d.status_word);
        d.status_valid = true;
        d.last_status_us = now_us;
        if ((d.status_word & 0x0008u) != 0) {
            latch_communication_fault(now_us, false);
            return;
        }
        break;
    }
    case OP_MOTOR_COMMAND:
        d.last_run_command = d.active_stw == V20_WORD_RUN;
        if (!d.last_run_command) d.pending_stop = false;
        d.last_command_us = now_us;
        break;
    default:
        break;
    }
    d.active_operation = OP_NONE;
}

#if SIMULATION_MODE
/* Keep the existing simulated wind-tunnel plant working during refactoring. */
extern void Simulation_SetOutput(float percentage);

static void simulate_response(int64_t now_us)
{
    if (now_us - d.tx_started_us < 1000) return;
    memset(d.rx, 0, sizeof(d.rx));
    d.rx[0] = MODBUS_SLAVE_ADDRESS;
    d.rx[1] = d.tx[1];
    if (d.rx[1] == V20_FC_WRITE_MULTI) {
        memcpy(&d.rx[2], &d.tx[2], 4);
        if (d.active_stw == V20_WORD_RUN) {
            const float pct = (float)d.active_hsw * 100.0f / 16384.0f;
            Simulation_SetOutput(pct);
            d.actual_hz = pct * 0.5f;
        } else {
            Simulation_SetOutput(0.0f);
            d.actual_hz = 0.0f;
        }
        d.rx_used = 8;
    } else {
        const uint16_t value = d.active_operation == OP_READ_WATCHDOG ?
            V20_EXPECTED_WATCHDOG_MS : V20_EXPECTED_REFERENCE_CENTIHZ;
        d.rx[2] = (uint8_t)(2 * d.active_count);
        if (d.active_operation == OP_READ_STATUS) {
            put_word(&d.rx[3], 0x0201u);
            put_word(&d.rx[5], (uint16_t)lroundf(d.actual_hz * 16384.0f / V20_REFERENCE_HZ));
        } else put_word(&d.rx[3], value);
        d.rx_used = d.rx_expected;
    }
    seal_frame(d.rx, d.rx_used - 2);
}
#endif

static void receive_pending(int64_t now_us)
{
#if SIMULATION_MODE
    simulate_response(now_us);
#else
    uint8_t bytes[V20_MAX_FRAME];
    /* timeout=0: read immediately available bytes only. */
    const int n = uart_read_bytes(MODBUS_UART_PORT, bytes, sizeof(bytes), 0);
    if (n < 0) { latch_communication_fault(now_us, false); return; }
    if (n > 0) {
        if (d.rx_used + (size_t)n > sizeof(d.rx)) {
            latch_communication_fault(now_us, false);
            return;
        }
        memcpy(d.rx + d.rx_used, bytes, (size_t)n);
        d.rx_used += (size_t)n;
    }
#endif
    if (d.rx_used) {
        /* A Modbus exception reply is 5 bytes instead of the normal length. */
        if (d.rx_used >= 2 && d.rx[1] == (uint8_t)(d.tx[1] | 0x80u)) {
            d.rx_expected = 5;
        }
        if (d.rx_used >= d.rx_expected) {
            if (!verify_reply()) latch_communication_fault(now_us, false);
            else accept_reply(now_us);
            return;
        }
    }
    if (now_us - d.tx_started_us >= (int64_t)V20_RESPONSE_TIMEOUT_MS * US_PER_MS)
        latch_communication_fault(now_us, true);
}

static void restart_commissioning(void)
{
    d.latched_fault = false;
    d.armed = false;
    d.configured = false;
    d.status_valid = false;
    d.drive_ready = false;
    d.pending_stop = true;
    d.last_run_command = false;
    d.bootstrap_next = OP_BOOT_STOP;
    d.recheck_pending = false;
    d.active_operation = OP_NONE;
    d.rx_used = 0;
    d.phase = PH_IDLE;
}

bool V20Modbus_Init(void)
{
    memset(&d, 0, sizeof(d));
#if SIMULATION_MODE
    d.uart_ok = true;
#else
    const uart_config_t cfg = {
        .baud_rate = MODBUS_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = MODBUS_PARITY,
        .stop_bits = MODBUS_STOP_BITS,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB
    };
    if (uart_param_config(MODBUS_UART_PORT, &cfg) != ESP_OK ||
        uart_set_pin(MODBUS_UART_PORT, MODBUS_TX_PIN, MODBUS_RX_PIN,
                     MODBUS_DE_PIN, UART_PIN_NO_CHANGE) != ESP_OK ||
        uart_driver_install(MODBUS_UART_PORT, V20_RX_BUFFER, V20_TX_BUFFER,
                            0, NULL, 0) != ESP_OK ||
        uart_set_mode(MODBUS_UART_PORT, UART_MODE_RS485_HALF_DUPLEX) != ESP_OK) {
        return false;
    }
    d.uart_ok = true;
#endif
    restart_commissioning();
    d.bus_quiet_until_us = esp_timer_get_time() + V20_FRAME_GAP_US;
    return true;
}

bool V20Modbus_Arm(void)
{
    const int64_t now_us = esp_timer_get_time();
    const bool good = d.uart_ok && d.configured && !d.latched_fault &&
        !d.recheck_pending && d.status_valid && d.drive_ready &&
        now_us - d.last_status_us <= (int64_t)MOTOR_STATUS_TIMEOUT_MS * US_PER_MS;
#if !SIMULATION_MODE && !V20_RUN_COMMISSIONED
    (void)good;
    return false;
#else
    if (!good) return false;
    d.armed = true;
    return true;
#endif
}

void V20Modbus_Disarm(void)
{
    d.armed = false;
    d.pending_stop = true;
}

void V20Modbus_RequestRecheck(void)
{
    /* Never abort an in-flight RTU transaction; defer reinitialisation. */
    V20Modbus_Disarm();
    d.recheck_pending = true;
}

void V20Modbus_Service(const MotorRequest *request, bool safety_ok)
{
    const int64_t now_us = esp_timer_get_time();
    if (!d.uart_ok) return;
    const bool fresh = request != NULL &&
        (TickType_t)(xTaskGetTickCount() - request->tick) <
            pdMS_TO_TICKS(V20_MAX_REQUEST_AGE_MS);
    const bool valid_percent = request != NULL && isfinite(request->output) &&
        request->output >= 0.0f && request->output <= (100.0f * MANUAL_MAX_HZ / V20_REFERENCE_HZ);
    const bool requested_run = fresh && valid_percent && request->run;
    if (!safety_ok || !fresh || !valid_percent || !requested_run) {
        /* A normal non-running request disarms motor output, never re-arms it. */
        if (d.armed || d.last_run_command || d.demand_run) V20Modbus_Disarm();
    }
    if (d.latched_fault) d.armed = false;
    d.demand_run = d.armed && requested_run && safety_ok && d.configured &&
        !d.latched_fault;

    if (d.phase == PH_RECOVERY) {
        if (now_us < d.bus_quiet_until_us) return;
        d.phase = PH_IDLE;
    }
    if (d.phase == PH_WAIT_RESPONSE) {
        receive_pending(now_us);
        return;
    }
    if (d.recheck_pending && now_us >= d.bus_quiet_until_us) {
#if !SIMULATION_MODE
        (void)uart_flush_input(MODBUS_UART_PORT);
#endif
        restart_commissioning();
    }
    if (d.latched_fault || now_us < d.bus_quiet_until_us) return;
    if (d.bootstrap_next != OP_NONE) {
        (void)send_operation(d.bootstrap_next, false, 0, now_us);
        return;
    }
    if (!d.configured) return;
    if (d.pending_stop || (!d.demand_run && d.last_run_command)) {
        (void)send_operation(OP_MOTOR_COMMAND, false, 0, now_us);
        return;
    }
    if (now_us - d.last_status_us >= (int64_t)V20_STATUS_PERIOD_MS * US_PER_MS ||
        !d.status_valid) {
        (void)send_operation(OP_READ_STATUS, false, 0, now_us);
        return;
    }
    if (d.demand_run) {
        if (now_us - d.last_command_us >= (int64_t)V20_COMMAND_PERIOD_MS * US_PER_MS)
            (void)send_operation(OP_MOTOR_COMMAND, true, request->output, now_us);
    } else if (now_us - d.last_command_us >= (int64_t)V20_STOP_HEARTBEAT_MS * US_PER_MS) {
        (void)send_operation(OP_MOTOR_COMMAND, false, 0, now_us);
    }
}

void V20Modbus_GetStatus(MotorStatus *status)
{
    if (status == NULL) return;
    const int64_t now_us = esp_timer_get_time();
    const bool recent = d.status_valid && d.last_status_us > 0 &&
        now_us - d.last_status_us < (int64_t)MOTOR_STATUS_TIMEOUT_MS * US_PER_MS;
    const bool healthy = d.configured && recent && d.drive_ready &&
        !d.latched_fault && !d.recheck_pending;
    *status = (MotorStatus){
#if SIMULATION_MODE || V20_RUN_COMMISSIONED
        .ready = healthy,
#else
        .ready = false,
#endif
        .ok = healthy,
        .actual_hz = d.actual_hz,
        .status = d.status_word,
        .errors = d.errors,
        .timeouts = d.timeouts,
        .tick = recent ? xTaskGetTickCount() : 0
    };
}

/* This is an operational STOP acknowledgement, not a safety function. */
bool V20Modbus_StopConfirmed(int64_t stop_entered_us, float maximum_hz)
{
    if (!isfinite(maximum_hz) || maximum_hz < 0.0f) return false;
    const int64_t now_us = esp_timer_get_time();
    return d.configured && d.status_valid && d.drive_ready &&
           !d.latched_fault && !d.recheck_pending &&
           !d.pending_stop && !d.last_run_command &&
           d.last_status_us > stop_entered_us &&
           now_us - d.last_status_us <
               (int64_t)MOTOR_STATUS_TIMEOUT_MS * US_PER_MS &&
           fabsf(d.actual_hz) <= maximum_hz;
}
