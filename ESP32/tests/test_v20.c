/*
 * test_v20.c
 * Public-API regression tests for the production non-blocking V20 driver.
 * UART and time are mocked; no production driver logic is replaced.
 *
 * Author: R.E. van Eekelen
 */
#include "V20Modbus.h"
#include "AppConfig.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////
// Mock transport and drive state

typedef struct
{
    uint16_t stw, hsw, status, watchdog, reference, actual;
    unsigned commands, run_commands, reads, init_calls;
    int init_failure, damage;
    bool fail_flush, fail_write, fail_read;
    unsigned chunk;
    uint8_t rx[32];
    size_t rx_size, rx_offset;
    int64_t last_tx_us;
} Fake;

static Fake fake;
static int64_t now_us;

int64_t esp_timer_get_time(void)
{
    return now_us;
}
TickType_t xTaskGetTickCount(void)
{
    return (TickType_t)(now_us / 1000);
}

static uint16_t word(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] << 8 | p[1]);
}

static void put(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

static int init_result(void)
{
    fake.init_calls++;
    return (int)fake.init_calls == fake.init_failure ? ESP_FAIL : ESP_OK;
}

esp_err_t uart_param_config(int port, const uart_config_t *config)
{
    assert(port == MODBUS_UART_PORT && config->baud_rate == MODBUS_BAUD);
    assert(config->data_bits == UART_DATA_8_BITS && config->parity == MODBUS_PARITY);
    assert(config->stop_bits == MODBUS_STOP_BITS);
    return init_result();
}

esp_err_t uart_set_pin(int port, int tx, int rx, int rts, int cts)
{
    assert(port == MODBUS_UART_PORT && tx == MODBUS_TX_PIN && rx == MODBUS_RX_PIN);
    assert(rts == MODBUS_DE_PIN && cts == UART_PIN_NO_CHANGE);
    return init_result();
}

esp_err_t uart_driver_install(int port, int rx_size, int tx_size, int queue_size, void *queue,
                              int flags)
{
    assert(port == MODBUS_UART_PORT && rx_size == 256 && tx_size == 256);
    assert(queue_size == 0 && queue == NULL && flags == 0);
    return init_result();
}

esp_err_t uart_set_mode(int port, int mode)
{
    assert(port == MODBUS_UART_PORT && mode == UART_MODE_RS485_HALF_DUPLEX);
    return init_result();
}

esp_err_t uart_flush_input(int port)
{
    assert(port == MODBUS_UART_PORT);
    fake.rx_size = fake.rx_offset = 0;
    return fake.fail_flush ? ESP_FAIL : ESP_OK;
}

/*
 * Validates every transmitted frame and queues a reply for a later service call.
 * Commissioning is read-only: FC06 writes are deliberately not accepted.
 */
int uart_write_bytes(int port, const void *data, size_t length)
{
    const uint8_t *tx = data;
    assert(port == MODBUS_UART_PORT && length >= 8);
    assert(now_us - fake.last_tx_us >= MODBUS_SILENCE_US);
    fake.last_tx_us = now_us;
    uint16_t crc = V20Modbus_Crc16(tx, length - 2);
    assert(tx[length - 2] == (uint8_t)crc && tx[length - 1] == (uint8_t)(crc >> 8));
    assert(tx[0] == MODBUS_SLAVE_ADDRESS);
    if (fake.fail_write)
        return -1;

    uint8_t *rx = fake.rx;
    memcpy(rx, tx, 6);
    size_t size = 8;
    if (tx[1] == 16)
    {
        assert(length == 13 && word(tx + 2) == 99 && word(tx + 4) == 2 && tx[6] == 4);
        fake.stw = word(tx + 7);
        fake.hsw = word(tx + 9);
        fake.commands++;
        assert(fake.stw == 0x047e || fake.stw == 0x047f);
        if (fake.stw == 0x047f)
            fake.run_commands++;
        else
            assert(fake.hsw == 0);
    }
    else
    {
        assert(tx[1] == 3 && length == 8);
        uint16_t address = word(tx + 2), count = word(tx + 4);
        rx[2] = (uint8_t)(2 * count);
        size = 5u + 2u * count;
        fake.reads++;
        if (address == 0)
        {
            assert(count == 1);
            put(rx + 3, fake.watchdog);
        }
        else if (address == 15)
        {
            assert(count == 1);
            put(rx + 3, fake.reference);
        }
        else
        {
            assert(address == 109 && count == 2);
            put(rx + 3, fake.status);
            put(rx + 5, fake.actual);
        }
    }

    int damage = fake.damage;
    fake.damage = 0;
    if (damage == 1)
        size = 0; /* Applied command, lost acknowledgement. */
    if (damage == 2)
        rx[0] = 2;
    if (damage == 3)
    {
        rx[1] = (uint8_t)(tx[1] | 0x80);
        rx[2] = 2;
        size = 5;
    }
    if (damage == 4)
        rx[3] ^= 1; /* Wrong write address with valid CRC. */
    if (damage == 6)
        rx[1] = 4;
    if (damage == 7)
        rx[5] ^= 1; /* Wrong write count with valid CRC. */
    if (size)
    {
        crc = V20Modbus_Crc16(rx, size - 2);
        rx[size - 2] = (uint8_t)crc;
        rx[size - 1] = (uint8_t)(crc >> 8);
        if (damage == 5)
            rx[size - 1] ^= 1;
        if (damage == 8)
            size--;
    }
    fake.rx_size = size;
    fake.rx_offset = 0;
    return (int)length;
}

int uart_read_bytes(int port, void *data, unsigned length, TickType_t wait)
{
    assert(port == MODBUS_UART_PORT && wait == 0);
    if (fake.fail_read)
        return -1;
    size_t count = fake.rx_size - fake.rx_offset;
    if (count > length)
        count = length;
    if (fake.chunk && count > fake.chunk)
        count = fake.chunk;
    memcpy(data, fake.rx + fake.rx_offset, count);
    fake.rx_offset += count;
    return (int)count;
}

///////////////////////////////////////////////////////////////////////////////
// Test helpers

static MotorStatus status(void)
{
    MotorStatus result;
    V20Modbus_GetStatus(&result);
    return result;
}

static MotorRequest request(bool run, float output)
{
    return (MotorRequest){
        .run = run, .output = output, .tick = xTaskGetTickCount(), .permissive = run, .epoch = 1};
}

static void step(MotorRequest *r, bool fresh, bool safe)
{
    now_us += CONTROL_PERIOD_US;
    if (r && fresh)
        r->tick = xTaskGetTickCount();
    V20Modbus_Service(r, safe);
}

static void pump(MotorRequest *r, unsigned count)
{
    for (unsigned i = 0; i < count; i++)
        step(r, true, true);
}

static void reset_mock(void)
{
    fake = (Fake){.reference = 5000, .watchdog = 500, .status = 0x0201};
    now_us = 1000000;
}

static void boot(void)
{
    reset_mock();
    assert(V20Modbus_Init());
    assert(!V20Modbus_Arm());   /* No status or commissioning verification yet. */
    assert(fake.commands == 0); /* Init never blocks on a telegram. */
    MotorRequest r = request(false, 0);
    pump(&r, 12);
    assert(status().ok && fake.stw == 0x047e && fake.hsw == 0);
    assert(fake.watchdog == 500 && fake.reference == 5000 && fake.run_commands == 0);
}

#if V20_RUN_COMMISSIONED
static void run_until_command(MotorRequest *r)
{
    unsigned before = fake.commands;
    for (unsigned i = 0; i < 60 && fake.commands == before; i++)
        step(r, true, true);
    assert(fake.commands > before);
    step(r, true, true); /* Acknowledge the command. */
}

/*
 * Recovery is an explicit recheck and a later Arm, not a RUN retry.
 * A fresh request or a new epoch never substitutes for that Arm.
 */
static void recover(void)
{
    unsigned before = fake.run_commands;
    MotorRequest r = request(true, 50);
    pump(&r, 50);
    assert(!V20Modbus_Arm() && fake.run_commands == before);
    V20Modbus_RequestRecheck();
    assert(!V20Modbus_Arm());
    pump(&r, 60);
    assert(status().ready && status().ok && fake.run_commands == before);
    assert(fake.stw == 0x047e);
    r.epoch++;
    pump(&r, 6);
    assert(fake.run_commands == before);
    assert(V20Modbus_Arm());
    run_until_command(&r);
    assert(fake.stw == 0x047f && fake.hsw == 8192);
}

static void test_run_stop(void)
{
    boot();
    MotorRequest r = request(true, 50);
    assert(V20Modbus_Arm());
    run_until_command(&r);
    assert(fake.stw == 0x047f && fake.hsw == 8192);
    r.output = 100;
    run_until_command(&r);
    assert(fake.hsw == 16384);
    r.output = 0;
    run_until_command(&r);
    assert(fake.stw == 0x047f && fake.hsw == 0);
    fake.actual = 8192;
    pump(&r, 30);
    assert(fabsf(status().actual_hz - 25.0f) < .001f);

    int64_t stopped_us = now_us;
    V20Modbus_Disarm();
    assert(!V20Modbus_StopConfirmed(stopped_us, .1f));
    r.run = false;
    r.permissive = true; /* Normal OFF1 keeps the operational K2 permissive. */
    r.output = 100;
    run_until_command(&r);
    assert(fake.stw == 0x047e && fake.hsw == 0);
    assert(!V20Modbus_StopConfirmed(stopped_us, .1f)); /* Speed still nonzero. */
    fake.actual = 0;
    pump(&r, 30);
    assert(V20Modbus_StopConfirmed(stopped_us, .1f));
    assert(!V20Modbus_StopConfirmed(now_us, .1f)); /* Old status is insufficient. */
    assert(!V20Modbus_StopConfirmed(stopped_us, NAN));
    assert(!V20Modbus_StopConfirmed(stopped_us, -1));
    unsigned runs = fake.run_commands;
    r.run = true;
    r.epoch++;
    pump(&r, 12);
    assert(fake.run_commands == runs); /* RESET/new epoch cannot re-arm. */
    now_us += (int64_t)MOTOR_STATUS_TIMEOUT_MS * 1000;
    assert(!status().ok && !V20Modbus_Arm());
    assert(!V20Modbus_StopConfirmed(stopped_us, .1f));
    puts("PASS V20 scaling, arm/disarm, OFF1, fresh stop confirmation and explicit re-arm");
}

static void test_errors(void)
{
    for (int damage = 1; damage <= 8; damage++)
    {
        boot();
        MotorRequest r = request(true, 50);
        assert(V20Modbus_Arm());
        fake.damage = damage;
        pump(&r, 24);
        assert(!status().ok && status().errors > 0 && !V20Modbus_Arm());
        if (damage == 1 || damage == 8)
            assert(status().timeouts > 0);
        recover();
    }
    for (int failure = 0; failure < 3; failure++)
    {
        boot();
        MotorRequest r = request(true, 50);
        assert(V20Modbus_Arm());
        fake.fail_flush = failure == 0;
        fake.fail_write = failure == 1;
        fake.fail_read = failure == 2;
        pump(&r, 24);
        assert(!status().ok && status().errors > 0 && !V20Modbus_Arm());
        fake.fail_flush = fake.fail_write = fake.fail_read = false;
        recover();
    }
    puts("PASS all eight legacy reply failures, UART failures, fault latch and explicit recovery");
}

static void test_interlocks(void)
{
    const float invalid[] = {NAN, 101, -1};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
    {
        boot();
        MotorRequest r = request(true, 50);
        assert(V20Modbus_Arm());
        run_until_command(&r);
        r.output = invalid[i];
        pump(&r, 12);
        assert(fake.stw == 0x047e && fake.hsw == 0);
        unsigned runs = fake.run_commands;
        r.output = 50;
        pump(&r, 12);
        assert(fake.run_commands == runs);
    }
    for (unsigned fault = 0; fault < 3; fault++)
    {
        boot();
        MotorRequest r = request(true, 50);
        assert(V20Modbus_Arm());
        run_until_command(&r);
        r.tick = xTaskGetTickCount() - 50; /* Exactly 50 ms is stale. */
        for (unsigned i = 0; i < 12; i++)
            step(fault == 2 ? NULL : &r, fault != 0, fault != 1);
        assert(fake.stw == 0x047e && fake.hsw == 0);
        unsigned runs = fake.run_commands;
        r.epoch = UINT32_MAX;
        pump(&r, 12);
        r.epoch = 0;
        pump(&r, 12);
        assert(fake.run_commands == runs);
    }
    puts("PASS invalid demand, stale/null requests, lost safety permissive and epoch-independent "
         "lockout");
}

static void test_status(void)
{
    const uint16_t bad[] = {0x0209, 0x0241, 0x0001, 0x0200};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
    {
        boot();
        MotorRequest r = request(false, 0);
        fake.status = bad[i];
        pump(&r, 30);
        assert(!status().ready && !status().ok && !V20Modbus_Arm());
        if (bad[i] & 8)
            assert(status().errors > 0);
        fake.status = 0x0201;
        V20Modbus_RequestRecheck();
        pump(&r, 60);
        assert(status().ready && fake.run_commands == 0);
    }
    boot();
    MotorRequest r = request(false, 0);
    fake.actual = 0xe000;
    pump(&r, 30);
    assert(status().actual_hz == -25);
    fake.actual = 0;
    fake.chunk = 1;
    pump(&r, 60);
    assert(status().ok && status().actual_hz == 0);
    puts("PASS unhealthy drive status, signed speed and fragmented replies");
}
#endif

///////////////////////////////////////////////////////////////////////////////
// Test entry point

int main(void)
{
    assert(V20Modbus_Crc16((const uint8_t *)"123456789", 9) == 0x4b37);
    assert(V20Modbus_Crc16(NULL, 0) == 0);
    V20Modbus_GetStatus(NULL);
    for (int failure = 1; failure <= 4; failure++)
    {
        reset_mock();
        fake.init_failure = failure;
        assert(!V20Modbus_Init());
        assert(!status().ready && !status().ok && !V20Modbus_Arm());
        MotorRequest r = request(true, 50);
        pump(&r, 30);
        assert(fake.commands == 0);
    }
    for (unsigned mismatch = 0; mismatch < 2; mismatch++)
    {
        reset_mock();
        if (mismatch)
            fake.watchdog = 0;
        else
            fake.reference = 6000;
        assert(V20Modbus_Init()); /* UART initialization, not drive health. */
        MotorRequest r = request(false, 0);
        pump(&r, 30);
        assert(!status().ready && !status().ok && status().errors > 0);
        assert(!V20Modbus_Arm() && fake.run_commands == 0);
    }
    puts("PASS UART initialization errors, CRC and read-only commissioning mismatches");
#if V20_RUN_COMMISSIONED
    test_run_stop();
    test_errors();
    test_interlocks();
    test_status();
#else
    boot();
    assert(!status().ready && !V20Modbus_Arm());
    MotorRequest r = request(true, 100);
    pump(&r, 60);
    assert(fake.run_commands == 0 && fake.stw == 0x047e);
    puts("PASS default uncommissioned firmware cannot arm or transmit RUN");
#endif
    return 0;
}
