///////////////////////////////////////////////////////////////////////////////
// File information

/*
 * V20Modbus.c
 *
 * Siemens SINAMICS V20: Modbus RTU protocol + UART/RS-485 transport.
 *
 * This entire module is serviced within ControlTask (Core 1, 10 ms).
 * No vTaskDelay(), blocking UART receive, or actuator FreeRTOS task.
 *
 * IMPORTANT: This is an operational motor interface, NOT a safety function.
 * A separately validated hardware safety circuit must inhibit motor torque
 * and allow the fan to coast on E-stop even if this MCU stops running.
 *
 * Author: R.E. van Eekelen
 */

///////////////////////////////////////////////////////////////////////////////
// System includes
#include "driver/uart.h"
#include "esp_timer.h"
#include <math.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////
// Application includes
#include "AppConfig.h"
#include "V20Modbus.h"

///////////////////////////////////////////////////////////////////////////////
// Local definitions

_Static_assert(V20_EXPECTED_REFERENCE_CENTIHZ == 5000, "V20 reference must remain 50 Hz");
_Static_assert(V20_REFERENCE_HZ == 50.0f && MANUAL_MAX_HZ <= 50.0f,
               "50 Hz command ceiling required");

#define V20_PDU_STW_HSW 99u  /* Holding registers 40100 and 40101. */
#define V20_PDU_ZSW_HIW 109u /* Holding registers 40110 and 40111. */
#define V20_PDU_WATCHDOG 0u  /* Holding register 40001. */
#define V20_PDU_REF_FREQ 15u /* Holding register 40016. */
#define V20_WORD_OFF1 0x047Eu
#define V20_WORD_RUN 0x047Fu
#define V20_STATUS_READY_BIT 0x0001u
#define V20_STATUS_OPERATION_ENABLED_BIT 0x0004u
#define V20_STATUS_FAULT_BIT 0x0008u
#define V20_STATUS_OFF2_BIT 0x0010u
#define V20_STATUS_OFF3_BIT 0x0020u
#define V20_STATUS_INHIBIT_BIT 0x0040u
#define V20_STATUS_REMOTE_BIT 0x0200u
#define V20_STATUS_READY_TO_OPERATE_BIT 0x0002u
#define V20_FC_READ 3u
#define V20_FC_WRITE_MULTI 16u
#define V20_MAX_FRAME 32u
#define V20_MAX_REQUEST_AGE_MS 50u
#define US_PER_MS 1000LL
#define V20_TX_BUFFER 256
#define V20_RX_BUFFER 256

/* The RTU inter-frame silence is fixed at 1.75 ms above 19.2 kbaud. */
#if MODBUS_BAUD > 19200
#define V20_FRAME_GAP_US 1750LL
#else
#define V20_FRAME_GAP_US ((38500000LL + MODBUS_BAUD - 1) / MODBUS_BAUD)
#endif

///////////////////////////////////////////////////////////////////////////////
// File-local data structures

typedef enum
{
    PH_IDLE = 0,
    PH_WAIT_RESPONSE,
    PH_RECOVERY
} Phase;

typedef struct
{
    bool uart_ok;
    bool configured;
    bool armed;
    bool latched_fault;
    bool status_valid;
    bool drive_ready;
    bool drive_fault_free;
    bool control_word_requested_valid, control_word_acknowledged_valid;
    bool reference_valid, reference_config_valid, watchdog_valid;
    bool watchdog_config_valid, watchdog_config_known, diagnostics_complete;
    bool pending_stop;
    bool recheck_pending;
    bool last_run_command;
    bool demand_run;
    V20Operation bootstrap_next;
    V20Operation active_operation;
    Phase phase;
    uint16_t status_word, control_word_requested, control_word_acknowledged;
    uint16_t reference_value, watchdog_register_value;
    float actual_hz;
    uint32_t errors, timeouts, exception_code, last_error_count;
    uint16_t last_error_register, last_error_expected, last_error_actual;
    V20CommError last_error;
    V20Operation last_operation;
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

///////////////////////////////////////////////////////////////////////////////
// File-local variables

static V20Context d;

///////////////////////////////////////////////////////////////////////////////
// V20Modbus_Crc16()

uint16_t V20Modbus_Crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFu;
    if (data == NULL)
        return 0;
    for (size_t i = 0; i < length; ++i)
    {
        crc ^= data[i];
        for (unsigned j = 0; j < 8; ++j)
        {
            crc = (uint16_t)((crc >> 1) ^ ((crc & 1u) ? 0xA001u : 0u));
        }
    }
    return crc;
}

///////////////////////////////////////////////////////////////////////////////
// put_word()

static void put_word(uint8_t *dst, uint16_t word)
{
    dst[0] = (uint8_t)(word >> 8);
    dst[1] = (uint8_t)word;
}

///////////////////////////////////////////////////////////////////////////////
// get_word()

static uint16_t get_word(const uint8_t *src)
{
    return (uint16_t)(((uint16_t)src[0] << 8) | src[1]);
}

///////////////////////////////////////////////////////////////////////////////
// seal_frame()

static size_t seal_frame(uint8_t *frame, size_t payload_size)
{
    const uint16_t crc = V20Modbus_Crc16(frame, payload_size);
    frame[payload_size] = (uint8_t)crc;
    frame[payload_size + 1] = (uint8_t)(crc >> 8);
    return payload_size + 2;
}

///////////////////////////////////////////////////////////////////////////////
// build_read()

static size_t build_read(uint8_t *dst, uint16_t address, uint16_t count)
{
    dst[0] = MODBUS_SLAVE_ADDRESS;
    dst[1] = V20_FC_READ;
    put_word(&dst[2], address);
    put_word(&dst[4], count);
    return seal_frame(dst, 6);
}

///////////////////////////////////////////////////////////////////////////////
// build_command()

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

///////////////////////////////////////////////////////////////////////////////
// V20Modbus_ErrorName()

const char *V20Modbus_ErrorName(V20CommError error)
{
    static const char *names[] = {"NONE",           "UART_INIT_FAILED", "UART_TX_FAILED",
                                  "UART_RX_FAILED", "TIMEOUT",          "CRC_ERROR",
                                  "INVALID_FRAME",  "MODBUS_EXCEPTION", "UNEXPECTED_REGISTER_VALUE",
                                  "STALE_RESPONSE"};
    return (unsigned)error < sizeof(names) / sizeof(names[0]) ? names[error] : "UNKNOWN";
}

///////////////////////////////////////////////////////////////////////////////
// V20Modbus_OperationName()

const char *V20Modbus_OperationName(V20Operation operation)
{
    static const char *names[] = {"NONE", "READ_STATUS", "READ_REFERENCE", "READ_WATCHDOG",
                                  "WRITE_CONTROL"};
    return (unsigned)operation < sizeof(names) / sizeof(names[0]) ? names[operation] : "UNKNOWN";
}

///////////////////////////////////////////////////////////////////////////////
// operation_register()

static uint16_t operation_register(V20Operation operation)
{
    switch (operation)
    {
    case V20_OP_READ_STATUS:
        return (uint16_t)(40001u + V20_PDU_ZSW_HIW);
    case V20_OP_READ_REFERENCE:
        return (uint16_t)(40001u + V20_PDU_REF_FREQ);
    case V20_OP_READ_WATCHDOG:
        return 40001u;
    case V20_OP_WRITE_CONTROL:
        return (uint16_t)(40001u + V20_PDU_STW_HSW);
    default:
        return 0;
    }
}

///////////////////////////////////////////////////////////////////////////////
// advance_bootstrap()

static void advance_bootstrap(V20Operation completed_operation)
{
    if (d.diagnostics_complete)
        return;
    switch (completed_operation)
    {
    case V20_OP_READ_STATUS:
        d.bootstrap_next = V20_OP_READ_REFERENCE;
        break;
    case V20_OP_READ_REFERENCE:
        d.bootstrap_next = V20_OP_READ_WATCHDOG;
        break;
    case V20_OP_READ_WATCHDOG:
        d.bootstrap_next = V20_OP_NONE;
        d.diagnostics_complete = true;
        break;
    default:
        break;
    }
}

///////////////////////////////////////////////////////////////////////////////
// record_error()

static void record_error(V20CommError error, uint16_t actual, uint32_t exception_code)
{
    d.last_error = error;
    d.last_operation = d.active_operation;
    d.last_error_register = operation_register(d.active_operation);
    d.last_error_expected = 0;
    if (d.active_operation == V20_OP_READ_REFERENCE)
        d.last_error_expected = V20_EXPECTED_REFERENCE_CENTIHZ;
#if V20_WATCHDOG_REGISTER_MAPPING_CONFIRMED
    else if (d.active_operation == V20_OP_READ_WATCHDOG)
        d.last_error_expected = V20_EXPECTED_WATCHDOG_REGISTER_VALUE;
#endif
    d.last_error_actual = actual;
    d.exception_code = exception_code;
    d.last_error_count = d.errors;
}

///////////////////////////////////////////////////////////////////////////////
// expected_pdu_address()

static uint16_t expected_pdu_address(V20Operation operation)
{
    switch (operation)
    {
    case V20_OP_READ_STATUS:
        return V20_PDU_ZSW_HIW;
    case V20_OP_READ_REFERENCE:
        return V20_PDU_REF_FREQ;
    case V20_OP_READ_WATCHDOG:
        return V20_PDU_WATCHDOG;
    case V20_OP_WRITE_CONTROL:
        return V20_PDU_STW_HSW;
    default:
        return 0;
    }
}

/* Preserve the current read in bootstrap mode so recovery can retry without
 * writes. */
///////////////////////////////////////////////////////////////////////////////
// fail_transaction()

static void fail_transaction(V20CommError error, int64_t now_us, uint16_t actual,
                             uint32_t exception_code)
{
    d.errors++;
    if (error == V20_COMM_TIMEOUT)
        d.timeouts++;
    if (d.active_operation == V20_OP_WRITE_CONTROL)
        d.control_word_acknowledged_valid = false;
    record_error(error, actual, exception_code);
    d.armed = false;
    d.demand_run = false;
    if (!d.diagnostics_complete)
    {
        d.bootstrap_next = d.active_operation;
    }
    else
    {
        d.latched_fault = true;
        d.pending_stop = true;
    }
    d.phase = PH_RECOVERY;
    d.bus_quiet_until_us = now_us + V20_FRAME_GAP_US;
    d.rx_used = 0;
    d.active_operation = V20_OP_NONE;
}

///////////////////////////////////////////////////////////////////////////////
// accept_exception()

static void accept_exception(int64_t now_us, uint32_t exception_code)
{
    d.errors++;
    d.last_rx_success_us = now_us;
    d.bus_quiet_until_us = now_us + V20_FRAME_GAP_US;
    d.phase = PH_IDLE;
    record_error(V20_COMM_MODBUS_EXCEPTION, 0, exception_code);
    if (d.active_operation == V20_OP_READ_REFERENCE)
    {
        d.reference_valid = false;
        d.reference_config_valid = false;
        d.configured = false;
    }
    else if (d.active_operation == V20_OP_READ_STATUS)
    {
        d.status_valid = false;
    }
    else if (d.active_operation == V20_OP_READ_WATCHDOG)
    {
        d.watchdog_valid = false;
        d.watchdog_config_known = V20_WATCHDOG_REGISTER_MAPPING_CONFIRMED;
        d.watchdog_config_valid = false;
    }
    else if (d.active_operation == V20_OP_WRITE_CONTROL)
    {
        d.control_word_acknowledged_valid = false;
        d.armed = false;
        d.demand_run = false;
        d.latched_fault = true;
        d.pending_stop = true;
    }

    advance_bootstrap(d.active_operation);
    d.active_operation = V20_OP_NONE;
    d.bus_quiet_until_us = now_us + (int64_t)V20_RESPONSE_TIMEOUT_MS * 2 * US_PER_MS;
}

/*
 * One bounded queue operation. TX buffering prevents waiting for the entire
 * frame to leave the UART, while the control loop only handles completed RX.
 * Only this module accesses the UART; no second task may share the port.
 */
///////////////////////////////////////////////////////////////////////////////
// send_operation()

static bool send_operation(V20Operation operation, bool run, float percent, int64_t now_us)
{
    if (d.phase != PH_IDLE || now_us < d.bus_quiet_until_us)
        return false;

    size_t size = 0;
    d.active_operation = operation;
    if (operation == V20_OP_WRITE_CONTROL)
    {
        uint16_t stw = run ? V20_WORD_RUN : V20_WORD_OFF1;
        uint16_t hsw = run ? (uint16_t)lroundf(percent * 16384.0f / 100.0f) : 0;
        size = build_command(d.tx, stw, hsw);
        d.active_stw = stw;
        d.active_hsw = hsw;
        d.control_word_requested = stw;
        d.control_word_requested_valid = true;
        d.control_word_acknowledged_valid = false;
        d.active_address = V20_PDU_STW_HSW;
        d.active_count = 2;
        d.rx_expected = 8;
    }
    else
    {
        const uint16_t address = expected_pdu_address(operation);
        const uint16_t count = operation == V20_OP_READ_STATUS ? 2 : 1;
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
        uart_write_bytes(MODBUS_UART_PORT, (const char *)d.tx, size) != (int)size)
    {
        fail_transaction(V20_COMM_UART_TX_FAILED, now_us, 0, 0);
        return false;
    }
#endif
    d.rx_used = 0;
    d.tx_started_us = now_us;
    d.phase = PH_WAIT_RESPONSE;
    return true;
}

///////////////////////////////////////////////////////////////////////////////
// verify_reply()

static V20CommError verify_reply(uint32_t *exception_code)
{
    const uint8_t *reply = d.rx;
    const size_t size = d.rx_used;
    *exception_code = 0;
    if (size != d.rx_expected || size < 5 || reply[0] != MODBUS_SLAVE_ADDRESS)
        return V20_COMM_INVALID_FRAME;
    const uint16_t crc = V20Modbus_Crc16(reply, size - 2);
    if (reply[size - 2] != (uint8_t)crc || reply[size - 1] != (uint8_t)(crc >> 8))
        return V20_COMM_CRC_ERROR;
    if (reply[1] == (d.tx[1] | 0x80u))
    {
        if (size != 5)
            return V20_COMM_INVALID_FRAME;
        *exception_code = reply[2];
        return V20_COMM_MODBUS_EXCEPTION;
    }
    if (reply[1] != d.tx[1])
        return V20_COMM_INVALID_FRAME;
    if (reply[1] == V20_FC_WRITE_MULTI)
    {
        return get_word(&reply[2]) == d.active_address && get_word(&reply[4]) == d.active_count
                   ? V20_COMM_NONE
                   : V20_COMM_INVALID_FRAME;
    }
    return reply[2] == 2u * d.active_count ? V20_COMM_NONE : V20_COMM_INVALID_FRAME;
}

///////////////////////////////////////////////////////////////////////////////
// accept_reply()

static void accept_reply(int64_t now_us)
{
    d.last_rx_success_us = now_us;
    d.bus_quiet_until_us = now_us + V20_FRAME_GAP_US;
    d.phase = PH_IDLE;

    switch (d.active_operation)
    {
    case V20_OP_READ_STATUS:
    {
        d.status_word = get_word(&d.rx[3]);
        const uint16_t raw = get_word(&d.rx[5]);
        const int32_t signed_raw = raw < 32768u ? (int32_t)raw : (int32_t)raw - 65536;
        d.actual_hz = (float)signed_raw * V20_REFERENCE_HZ / 16384.0f;
        d.drive_fault_free = (d.status_word & V20_STATUS_FAULT_BIT) == 0;
        /* OFF2/OFF3 and inhibit bits must be clear before the app treats the drive
         * as start-ready.
         */
        d.drive_ready = (d.status_word & (V20_STATUS_READY_BIT | V20_STATUS_REMOTE_BIT |
                                          V20_STATUS_OFF2_BIT | V20_STATUS_OFF3_BIT)) ==
                            (V20_STATUS_READY_BIT | V20_STATUS_REMOTE_BIT | V20_STATUS_OFF2_BIT |
                             V20_STATUS_OFF3_BIT) &&
                        (d.status_word & (V20_STATUS_FAULT_BIT | V20_STATUS_INHIBIT_BIT)) == 0;
        d.status_valid = true;
        d.last_status_us = now_us;
        break;
    }
    case V20_OP_READ_REFERENCE:
        d.reference_value = get_word(&d.rx[3]);
        d.reference_valid = true;
        d.reference_config_valid = d.reference_value == V20_EXPECTED_REFERENCE_CENTIHZ;
        d.configured = d.reference_config_valid;
        if (!d.reference_config_valid)
            record_error(V20_COMM_UNEXPECTED_REGISTER_VALUE, d.reference_value, 0);
        break;
    case V20_OP_READ_WATCHDOG:
        d.watchdog_register_value = get_word(&d.rx[3]);
        d.watchdog_valid = true;
        d.watchdog_config_known = V20_WATCHDOG_REGISTER_MAPPING_CONFIRMED;
#if V20_WATCHDOG_REGISTER_MAPPING_CONFIRMED
        d.watchdog_config_valid = d.watchdog_register_value == V20_EXPECTED_WATCHDOG_REGISTER_VALUE;
        if (!d.watchdog_config_valid)
            record_error(V20_COMM_UNEXPECTED_REGISTER_VALUE, d.watchdog_register_value, 0);
#else
        d.watchdog_config_valid = false;
#endif
        break;
    case V20_OP_WRITE_CONTROL:
        d.last_run_command = d.active_stw == V20_WORD_RUN;
        d.control_word_acknowledged = d.active_stw;
        d.control_word_acknowledged_valid = true;
        if (!d.last_run_command)
            d.pending_stop = false;
        d.last_command_us = now_us;
        break;
    default:
        break;
    }
    advance_bootstrap(d.active_operation);
    d.active_operation = V20_OP_NONE;
}

#if SIMULATION_MODE
/* Keep the existing simulated wind-tunnel plant working during refactoring. */
extern void Simulation_SetOutput(float percentage);

///////////////////////////////////////////////////////////////////////////////
// simulate_response()

static void simulate_response(int64_t now_us)
{
    if (now_us - d.tx_started_us < 1000)
        return;
    memset(d.rx, 0, sizeof(d.rx));
    d.rx[0] = MODBUS_SLAVE_ADDRESS;
    d.rx[1] = d.tx[1];
    if (d.rx[1] == V20_FC_WRITE_MULTI)
    {
        memcpy(&d.rx[2], &d.tx[2], 4);
        if (d.active_stw == V20_WORD_RUN)
        {
            const float pct = (float)d.active_hsw * 100.0f / 16384.0f;
            Simulation_SetOutput(pct);
            d.actual_hz = pct * 0.5f;
        }
        else
        {
            Simulation_SetOutput(0.0f);
            d.actual_hz = 0.0f;
        }
        d.rx_used = 8;
    }
    else
    {
        const uint16_t value = d.active_operation == V20_OP_READ_WATCHDOG
                                   ? (uint16_t)V20_EXPECTED_WATCHDOG_PARAMETER_MS
                                   : V20_EXPECTED_REFERENCE_CENTIHZ;
        d.rx[2] = (uint8_t)(2 * d.active_count);
        if (d.active_operation == V20_OP_READ_STATUS)
        {
            put_word(&d.rx[3], 0x0231u);
            put_word(&d.rx[5], (uint16_t)lroundf(d.actual_hz * 16384.0f / V20_REFERENCE_HZ));
        }
        else
            put_word(&d.rx[3], value);
        d.rx_used = d.rx_expected;
    }
    seal_frame(d.rx, d.rx_used - 2);
}
#endif

///////////////////////////////////////////////////////////////////////////////
// receive_pending()

static void receive_pending(int64_t now_us)
{
#if SIMULATION_MODE
    simulate_response(now_us);
#else
    uint8_t bytes[V20_MAX_FRAME];
    /* timeout=0: read immediately available bytes only. */
    const int n = uart_read_bytes(MODBUS_UART_PORT, bytes, sizeof(bytes), 0);
    if (n < 0)
    {
        fail_transaction(V20_COMM_UART_RX_FAILED, now_us, 0, 0);
        return;
    }
    if (n > 0)
    {
        if (d.rx_used + (size_t)n > sizeof(d.rx))
        {
            fail_transaction(V20_COMM_INVALID_FRAME, now_us, 0, 0);
            return;
        }
        memcpy(d.rx + d.rx_used, bytes, (size_t)n);
        d.rx_used += (size_t)n;
    }
#endif
    if (d.rx_used)
    {
        /* A Modbus exception reply is 5 bytes instead of the normal length. */
        if (d.rx_used >= 2 && d.rx[1] == (uint8_t)(d.tx[1] | 0x80u))
        {
            d.rx_expected = 5;
        }
        if (d.rx_used >= d.rx_expected)
        {
            uint32_t exception_code = 0;
            V20CommError error = verify_reply(&exception_code);
            if (error == V20_COMM_NONE)
                accept_reply(now_us);
            else if (error == V20_COMM_MODBUS_EXCEPTION)
                accept_exception(now_us, exception_code);
            else
                fail_transaction(error, now_us, 0, exception_code);
            return;
        }
    }
    if (now_us - d.tx_started_us >= (int64_t)V20_RESPONSE_TIMEOUT_MS * US_PER_MS)
        fail_transaction(V20_COMM_TIMEOUT, now_us, 0, 0);
}

///////////////////////////////////////////////////////////////////////////////
// restart_commissioning()

static void restart_commissioning(void)
{
    d.latched_fault = false;
    d.armed = false;
    d.status_valid = false;
    d.drive_ready = false;
    d.drive_fault_free = false;
    d.reference_valid = false;
    d.reference_config_valid = false;
    d.configured = false;
    d.watchdog_valid = false;
    d.watchdog_config_valid = false;
    d.watchdog_config_known = false;
    d.diagnostics_complete = false;
    d.demand_run = false;
    d.last_run_command = false;
    d.bootstrap_next = V20_OP_READ_STATUS;
    d.recheck_pending = false;
    d.active_operation = V20_OP_NONE;
    d.rx_used = 0;
    d.phase = PH_IDLE;
    d.last_command_us = esp_timer_get_time();
}

///////////////////////////////////////////////////////////////////////////////
// V20Modbus_Init()

bool V20Modbus_Init(void)
{
    memset(&d, 0, sizeof(d));
#if SIMULATION_MODE
    d.uart_ok = true;
#else
    const uart_config_t cfg = {.baud_rate = MODBUS_BAUD,
                               .data_bits = UART_DATA_8_BITS,
                               .parity = MODBUS_PARITY,
                               .stop_bits = MODBUS_STOP_BITS,
                               .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
                               .source_clk = UART_SCLK_APB};
    if (uart_param_config(MODBUS_UART_PORT, &cfg) != ESP_OK ||
        uart_set_pin(MODBUS_UART_PORT, MODBUS_TX_PIN, MODBUS_RX_PIN, MODBUS_DE_PIN,
                     UART_PIN_NO_CHANGE) != ESP_OK ||
        uart_driver_install(MODBUS_UART_PORT, V20_RX_BUFFER, V20_TX_BUFFER, 0, NULL, 0) != ESP_OK ||
        uart_set_mode(MODBUS_UART_PORT, UART_MODE_RS485_HALF_DUPLEX) != ESP_OK)
    {
        record_error(V20_COMM_UART_INIT_FAILED, 0, 0);
        return false;
    }
    d.uart_ok = true;
#endif
    restart_commissioning();
    d.bus_quiet_until_us = esp_timer_get_time() + V20_FRAME_GAP_US;
    return true;
}

///////////////////////////////////////////////////////////////////////////////
// V20Modbus_Arm()

bool V20Modbus_Arm(void)
{
#if !SIMULATION_MODE && !V20_RUN_COMMISSIONED
    return false;
#else
    const int64_t now_us = esp_timer_get_time();
    const bool good = d.uart_ok && d.diagnostics_complete && d.reference_config_valid &&
                      (!V20_WATCHDOG_REGISTER_MAPPING_CONFIRMED || d.watchdog_config_valid) &&
                      !d.latched_fault && !d.recheck_pending && d.status_valid &&
                      d.drive_fault_free && d.drive_ready &&
                      now_us - d.last_status_us < (int64_t)MOTOR_STATUS_TIMEOUT_MS * US_PER_MS;
    if (!good)
        return false;
    d.armed = true;
    return true;
#endif
}

///////////////////////////////////////////////////////////////////////////////
// V20Modbus_Disarm()

void V20Modbus_Disarm(void)
{
    const bool stop_required = d.pending_stop || d.demand_run || d.last_run_command;
    d.armed = false;
    d.demand_run = false;
    d.pending_stop = stop_required;
}

///////////////////////////////////////////////////////////////////////////////
// V20Modbus_RequestRecheck()

void V20Modbus_RequestRecheck(void)
{
    /* Never abort an in-flight RTU transaction; defer reinitialisation. */
    V20Modbus_Disarm();
    d.recheck_pending = true;
}

///////////////////////////////////////////////////////////////////////////////
// V20Modbus_Service()

void V20Modbus_Service(const MotorRequest *request, bool safety_ok)
{
    const int64_t now_us = esp_timer_get_time();
    if (!d.uart_ok)
        return;
    const bool fresh = request != NULL && (TickType_t)(xTaskGetTickCount() - request->tick) <
                                              pdMS_TO_TICKS(V20_MAX_REQUEST_AGE_MS);
    const bool valid_percent = request != NULL && isfinite(request->output) &&
                               request->output >= 0.0f &&
                               request->output <= (100.0f * MANUAL_MAX_HZ / V20_REFERENCE_HZ);
    const bool requested_run = fresh && valid_percent && request->run;
    if (!safety_ok || !fresh || !valid_percent || !requested_run)
    {
        /* A normal non-running request disarms motor output, never re-arms it. */
        if (d.armed || d.last_run_command || d.demand_run)
            V20Modbus_Disarm();
    }
    if (d.latched_fault)
        d.armed = false;
    d.demand_run = d.armed && requested_run && safety_ok && d.configured && !d.latched_fault;

    if (d.phase == PH_RECOVERY)
    {
        if (now_us < d.bus_quiet_until_us)
            return;
        d.phase = PH_IDLE;
    }
    if (d.phase == PH_WAIT_RESPONSE)
    {
        receive_pending(now_us);
        return;
    }
    if (d.recheck_pending && now_us >= d.bus_quiet_until_us)
    {
#if !SIMULATION_MODE
        (void)uart_flush_input(MODBUS_UART_PORT);
#endif
        restart_commissioning();
    }
    if (d.last_rx_success_us > 0 &&
        now_us - d.last_rx_success_us >= (int64_t)MOTOR_STATUS_TIMEOUT_MS * US_PER_MS &&
        d.last_error != V20_COMM_STALE_RESPONSE)
    {
        d.errors++;
        record_error(V20_COMM_STALE_RESPONSE, 0, 0);
        d.armed = false;
        d.latched_fault = d.diagnostics_complete;
    }
    if (d.latched_fault || now_us < d.bus_quiet_until_us)
        return;
    if (d.bootstrap_next != V20_OP_NONE)
    {
        (void)send_operation(d.bootstrap_next, false, 0, now_us);
        return;
    }
    if (!d.diagnostics_complete)
        return;
    const bool operation_commissioned =
        (SIMULATION_MODE || V20_RUN_COMMISSIONED) && d.reference_config_valid &&
        (!V20_WATCHDOG_REGISTER_MAPPING_CONFIRMED || d.watchdog_config_valid);
    if (!operation_commissioned)
    {
        if (!d.status_valid ||
            now_us - d.last_status_us >= (int64_t)V20_STATUS_PERIOD_MS * US_PER_MS)
            (void)send_operation(V20_OP_READ_STATUS, false, 0, now_us);
        return;
    }
    if (!d.configured)
        return;
    if (!d.status_valid || now_us - d.last_status_us >= (int64_t)V20_STATUS_PERIOD_MS * US_PER_MS)
    {
        (void)send_operation(V20_OP_READ_STATUS, false, 0, now_us);
        return;
    }
    if (d.pending_stop || (!d.demand_run && d.last_run_command))
    {
        (void)send_operation(V20_OP_WRITE_CONTROL, false, 0, now_us);
        return;
    }
    if (d.demand_run)
    {
        if (now_us - d.last_command_us >= (int64_t)V20_COMMAND_PERIOD_MS * US_PER_MS)
            (void)send_operation(V20_OP_WRITE_CONTROL, true, request->output, now_us);
    }
    else if (now_us - d.last_command_us >= (int64_t)V20_STOP_HEARTBEAT_MS * US_PER_MS)
    {
        (void)send_operation(V20_OP_WRITE_CONTROL, false, 0, now_us);
    }
}

///////////////////////////////////////////////////////////////////////////////
// V20Modbus_GetStatus()

void V20Modbus_GetStatus(MotorStatus *status)
{
    if (status == NULL)
        return;
    const int64_t now_us = esp_timer_get_time();
    const bool recent = d.status_valid && d.last_status_us > 0 &&
                        now_us - d.last_status_us < (int64_t)MOTOR_STATUS_TIMEOUT_MS * US_PER_MS;
    const bool communication_recent =
        d.last_rx_success_us > 0 &&
        now_us - d.last_rx_success_us < (int64_t)MOTOR_STATUS_TIMEOUT_MS * US_PER_MS;
    const bool communication_ok =
        d.uart_ok && communication_recent && !d.latched_fault && !d.recheck_pending;
    const bool healthy =
        communication_ok && d.drive_fault_free && d.reference_config_valid && !d.latched_fault;
    const uint64_t age_us = d.last_rx_success_us > 0 ? (uint64_t)(now_us - d.last_rx_success_us)
                                                     : UINT32_MAX * (uint64_t)US_PER_MS;
    *status = (MotorStatus){
        .ready = d.diagnostics_complete && d.reference_config_valid &&
                 (!V20_WATCHDOG_REGISTER_MAPPING_CONFIRMED || d.watchdog_config_valid) && recent &&
                 d.drive_ready && !d.latched_fault,
        .ok = healthy,
        .initialized = d.uart_ok,
        .response_received = communication_recent,
        .status_healthy = d.drive_fault_free && recent,
        .communication_ok = communication_ok,
        .drive_fault_free = d.drive_fault_free && recent,
        .operational_ready = d.drive_ready && recent && d.reference_config_valid &&
                             (!V20_WATCHDOG_REGISTER_MAPPING_CONFIRMED || d.watchdog_config_valid),
        .ready_to_switch_on = recent && (d.status_word & V20_STATUS_READY_BIT) != 0,
        .ready_to_operate = recent && (d.status_word & V20_STATUS_READY_TO_OPERATE_BIT) != 0,
        .operation_enabled = recent && (d.status_word & V20_STATUS_OPERATION_ENABLED_BIT) != 0,
        .off2_active = recent && (d.status_word & V20_STATUS_OFF2_BIT) == 0,
        .off3_active = recent && (d.status_word & V20_STATUS_OFF3_BIT) == 0,
        .switch_on_inhibited = recent && (d.status_word & V20_STATUS_INHIBIT_BIT) != 0,
        .control_word_requested_valid = d.control_word_requested_valid,
        .control_word_acknowledged_valid = d.control_word_acknowledged_valid,
        .run_authorized = d.armed && d.demand_run,
        .diagnostics_complete = d.diagnostics_complete,
        .status_valid = d.status_valid && recent,
        .reference_valid = d.reference_valid,
        .reference_config_valid = d.reference_config_valid,
        .watchdog_valid = d.watchdog_valid,
        .watchdog_config_known = d.watchdog_config_known,
        .watchdog_config_valid = d.watchdog_config_valid,
        .actual_hz = d.actual_hz,
        .status = d.status_word,
        .control_word_requested = d.control_word_requested,
        .control_word_acknowledged = d.control_word_acknowledged,
        .reference_value = d.reference_value,
        .watchdog_register_value = d.watchdog_register_value,
        .watchdog_parameter_expected_value = V20_EXPECTED_WATCHDOG_PARAMETER_MS,
        .last_error_register = d.last_error_register,
        .last_error_expected = d.last_error_expected,
        .last_error_actual = d.last_error_actual,
        .errors = d.errors,
        .timeouts = d.timeouts,
        .last_success_age_ms =
            d.last_rx_success_us > 0 ? (uint32_t)(age_us / US_PER_MS) : UINT32_MAX,
        .exception_code = d.exception_code,
        .last_error_count = d.last_error_count,
        .last_error = (uint8_t)d.last_error,
        .last_operation = (uint8_t)d.last_operation,
        .tick = recent ? xTaskGetTickCount() : 0};
}

/* This is an operational STOP acknowledgement, not a safety function. */
///////////////////////////////////////////////////////////////////////////////
// V20Modbus_StopConfirmed()

bool V20Modbus_StopConfirmed(int64_t stop_entered_us, float maximum_hz)
{
    if (!isfinite(maximum_hz) || maximum_hz < 0.0f)
        return false;
    const int64_t now_us = esp_timer_get_time();
    return d.configured && d.status_valid && d.drive_ready && !d.latched_fault &&
           !d.recheck_pending && !d.pending_stop && !d.last_run_command &&
           d.last_status_us > stop_entered_us &&
           now_us - d.last_status_us < (int64_t)MOTOR_STATUS_TIMEOUT_MS * US_PER_MS &&
           fabsf(d.actual_hz) <= maximum_hz;
}
