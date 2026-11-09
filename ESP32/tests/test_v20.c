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
    unsigned commands, run_commands, reads, good_status_responses, init_calls;
    int init_failure, damage;
    bool fail_flush, fail_write, fail_read;
    unsigned chunk;
    uint8_t rx[32];
    size_t rx_size, rx_offset;
    int64_t last_tx_us, rx_available_us;
    uint32_t enqueue_delay_us, response_delay_us;
} Fake;

static Fake fake;
static Queue uart_event_queue;
static int injected_uart_event;
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
    assert(queue_size == 16 && queue != NULL && flags == 0);
    *(QueueHandle_t *)queue = &uart_event_queue;
    return init_result();
}

int xQueueReceive(QueueHandle_t queue, void *event, TickType_t wait)
{
    assert(queue==&uart_event_queue && wait==0);
    if (!injected_uart_event) return pdFALSE;
    *(uart_event_t *)event=(uart_event_t){.type=injected_uart_event};
    injected_uart_event=0;
    return pdTRUE;
}
esp_err_t uart_get_buffered_data_len(int port, size_t *size)
{
    assert(port==MODBUS_UART_PORT);
    *size=fake.rx_size-fake.rx_offset;
    return ESP_OK;
}

esp_err_t uart_driver_delete(int port) { assert(port == MODBUS_UART_PORT); return ESP_OK; }

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
    now_us += fake.enqueue_delay_us;
    fake.last_tx_us = now_us;
    fake.rx_available_us = now_us + fake.response_delay_us;
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
        assert(fake.reads >= 3); /* Read-only status/reference/watchdog bootstrap comes first. */
        assert(fake.good_status_responses > 0);
        fake.stw = word(tx + 7);
        fake.hsw = word(tx + 9);
        fake.commands++;
        assert(fake.stw == 0x047e || fake.stw == 0x047f || fake.stw == 0x04fe);
        if (fake.stw == 0x047f)
            fake.run_commands++;
        else {
            assert(fake.hsw == 0);
            if (fake.stw == 0x04fe)
                fake.status &= 0xfff7u;
        }
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
    if (tx[1] == 3 && word(tx + 2) == 109 && damage == 0)
        fake.good_status_responses++;
    fake.rx_size = size;
    fake.rx_offset = 0;
    return (int)length;
}

int uart_read_bytes(int port, void *data, unsigned length, TickType_t wait)
{
    assert(port == MODBUS_UART_PORT && wait == 0);
    if (fake.fail_read)
        return -1;
    if (now_us < fake.rx_available_us) return 0;
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
    fake = (Fake){.reference = 5000, .watchdog = 1000, .status = 0x0231};
    now_us = 1000000;
    injected_uart_event=0;
}

static void boot(void)
{
    reset_mock();
    assert(V20Modbus_Init());
    assert(!V20Modbus_Arm());   /* No status or commissioning verification yet. */
    V20Modbus_Disarm();
    assert(fake.commands == 0 && fake.reads == 0); /* No eager control-word write. */
    MotorRequest r = request(false, 0);
    pump(&r, 12);
    assert(status().communication_ok && status().ok);
    assert(status().ready && status().operational_ready && !status().run_authorized);
    assert(status().ready_to_switch_on && !status().ready_to_operate &&
           !status().operation_enabled);
    assert(!status().off2_active && !status().off3_active && !status().switch_on_inhibited);
    assert(fake.run_commands == 0);
#if V20_RUN_COMMISSIONED
    assert(fake.commands == 1 && fake.stw == 0x047e);
#else
    assert(fake.commands == 0);
#endif
    assert(status().watchdog_valid && status().watchdog_register_value == 1000 &&
           status().watchdog_parameter_expected_value == 500 &&
           !status().watchdog_config_known && fake.reference == 5000);
}

#if V20_RUN_COMMISSIONED
static void test_nonrunning_wait_heartbeat(void)
{
    boot();
    MotorRequest r = request(false, 0);
    pump(&r, 40);
    MotorStatus current = status();
    for (unsigned i=0; i<8 && !current.control_word_acknowledged_valid; ++i)
    { step(&r,true,true); current=status(); }
    assert(current.control_word_requested_valid &&
           current.control_word_acknowledged_valid);
    assert(current.control_word_requested == 0x047E &&
           current.control_word_acknowledged == 0x047E);
    assert(current.ready_to_switch_on && !current.operation_enabled);
    assert(!current.run_authorized && fake.run_commands == 0);
}

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

static void test_eb31_start_handshake(void)
{
    boot();
    MotorRequest r=request(false,0);
    fake.status=0xeb31; pump(&r,12);
    MotorStatus m=status();
    assert(m.ready && m.ok && !m.operation_enabled && !m.off2_active && !m.switch_on_inhibited);
    assert(!m.watchdog_config_known && !m.watchdog_config_valid);
    assert(fake.stw==0x047e && V20Modbus_Arm());
    r.run=true; r.permissive=true;
    run_until_command(&r);
    assert(fake.stw==0x047f && fake.hsw==0);
    fake.status=0xeb37; pump(&r,12);
    assert(status().operation_enabled && status().run_authorized);
    V20Modbus_Disarm(); r.run=false; pump(&r,12);
    unsigned runs=fake.run_commands;
    r.run=true; pump(&r,12);
    assert(fake.run_commands==runs); /* No implicit re-arm. */
    puts("PASS exact EB31 OFF1 -> physical-arm RUN/zero -> enabled EB37, no implicit restart");
}

static void test_cyclic_controlled_stop(void)
{
    boot(); MotorRequest r=request(true,50); assert(V20Modbus_Arm());
    run_until_command(&r);
    V20Modbus_Disarm(); r.run=false; r.permissive=true; fake.actual=8192;
    unsigned commands_before=fake.commands, runs=fake.run_commands;
    int64_t previous=0; uint32_t max_gap=0;
    for(unsigned i=0;i<1000;++i)
    {
        step(&r,true,true);
        if(fake.commands!=commands_before)
        {
            if(previous && (uint32_t)((now_us-previous)/1000)>max_gap)
                max_gap=(uint32_t)((now_us-previous)/1000);
            previous=now_us; commands_before=fake.commands;
            assert(fake.stw==0x047e && fake.hsw==0);
        }
    }
    assert(max_gap<=V20_COMMAND_PERIOD_MS+CONTROL_PERIOD_US/1000u);
    assert(fake.run_commands==runs && status().errors==0 && status().valid_reads>10);
    assert(!V20Modbus_StopConfirmed(0,.1f)); /* Frequency is still nonzero. */
    printf("PASS 10s controlled STOP, cyclic OFF1/zero gap=%lu ms, status polling continues\n",(unsigned long)max_gap);
}

static void test_transaction_timing_and_uart_errors(void)
{
    boot(); MotorRequest r=request(true,50); assert(V20Modbus_Arm());
    fake.enqueue_delay_us=7000; fake.response_delay_us=70000;
    unsigned before=fake.run_commands;
    while(fake.run_commands==before) step(&r,true,true);
    MotorStatus m=status();
    const uint32_t id=m.transaction.id;
    assert(m.transaction.pending && m.transaction.type==V20_OP_WRITE_CONTROL);
    assert(m.transaction.enqueue_us==7000);
    assert(m.transaction.start_ms==(uint32_t)(now_us/1000));
    assert(m.transaction.deadline_ms-m.transaction.start_ms==V20_RESPONSE_TIMEOUT_MS);
    assert(m.transaction.pdu_address==99 && m.transaction.register_count==2);
    pump(&r,6); assert(status().transaction.pending && status().timeouts==0);
    step(&r,true,true); m=status();
    assert(m.transaction.id==id && !m.transaction.pending && !m.transaction.write_unconfirmed);
    assert(m.transaction.rtt_us==70000 && m.transaction.rx_bytes==8 && m.transaction.result==V20_COMM_NONE);
    /* Partial echo at the deadline cannot be treated as a successful write. */
    fake.enqueue_delay_us=fake.response_delay_us=0;
    before=fake.run_commands; while(fake.run_commands==before) step(&r,true,true);
    fake.rx_size=3;
    step(&r,true,true); m=status();
    assert(m.transaction.pending && m.transaction.rx_bytes==3 &&
           !memcmp(m.transaction.rx_frame,fake.rx,3));
    pump(&r,7); m=status();
    assert(m.timeouts==1 && m.transaction.write_unconfirmed && m.transaction.rx_bytes==3);
    assert(m.transaction.result==V20_COMM_TIMEOUT && !m.communication_ok && m.run_locked);
    /* Repeated lost RUN echoes on the same driver instance. Each recovery
     * requires an explicit recheck and arm through the existing test fixture. */
    for (unsigned episode = 0; episode < 32; ++episode)
    {
        recover();
        before = fake.run_commands;
        while (fake.run_commands == before)
            step(&r, true, true);
        fake.rx_size = fake.rx_offset = 0;
        unsigned timeouts = status().timeouts;
        pump(&r, 8);
        m = status();
        assert(m.timeouts == timeouts + 1 && m.transaction.result == V20_COMM_TIMEOUT);
        assert(m.transaction.write_unconfirmed && m.transaction.rx_bytes == 0 && m.run_locked);
    }
    const int uart_error_events[]={UART_FIFO_OVF,UART_BUFFER_FULL,UART_PARITY_ERR,UART_FRAME_ERR};
    for(unsigned i=0;i<4;++i)
    {
        boot(); r=request(true,50); assert(V20Modbus_Arm());
        before=fake.run_commands; while(fake.run_commands==before) step(&r,true,true);
        injected_uart_event=uart_error_events[i]; step(&r,true,true); m=status();
        assert(m.transaction.result==V20_COMM_UART_RX_FAILED && m.transaction.uart_error_flags==(1u<<i));
        assert(m.transaction.write_unconfirmed && !m.communication_ok && m.run_locked);
    }
    puts("PASS measured enqueue/70ms response, unchanged 80ms window, partial ACK timeout and UART errors fail closed");
}

static void test_constant_process_data(void)
{
    boot();
    MotorRequest r = request(true, 100);
    assert(V20Modbus_Arm());
    unsigned writes = fake.run_commands;
    int64_t previous_tx = 0;
    uint32_t max_tx_gap = 0;
    /* Ten minutes, constant 50 Hz, no new HMI commands. */
    for (unsigned i=0; i<60000; ++i)
    {
        step(&r, true, true);
        if (fake.run_commands != writes)
        {
            if (previous_tx > 0)
            {
                uint32_t gap=(uint32_t)((now_us-previous_tx)/1000);
                if (gap > max_tx_gap) max_tx_gap=gap;
            }
            previous_tx=now_us;
            writes=fake.run_commands;
        }
    }
    MotorStatus m=status();
    assert(writes >= 600000u/(V20_COMMAND_PERIOD_MS + CONTROL_PERIOD_US/1000u)-5u &&
           max_tx_gap<=V20_COMMAND_PERIOD_MS + CONTROL_PERIOD_US/1000u);
    assert(m.valid_reads>1000 && m.valid_writes >= 600000u/(V20_COMMAND_PERIOD_MS + CONTROL_PERIOD_US/1000u)-5u);
    /* Lifetime maxima also include stopped/bootstrap/transition intervals. */
    assert(m.max_process_data_gap_ms <= V20_STOP_HEARTBEAT_MS + 4u * (CONTROL_PERIOD_US/1000u));
    assert(m.max_response_gap_ms <= V20_STATUS_PERIOD_MS + 4u * (CONTROL_PERIOD_US/1000u));
    assert(m.last_valid_response_ms && m.last_valid_process_data_ms && m.last_process_data_tx_ms);
    assert(m.requested_hz==50 && m.errors==0 && m.timeouts==0);
    /* A successful read is not a successful process write; missing echo never counts. */
    while (fake.run_commands==writes) step(&r,true,true);
    unsigned confirmed=status().valid_writes;
    uint8_t delayed[32];
    const size_t delayed_size=fake.rx_size;
    memcpy(delayed,fake.rx,delayed_size);
    fake.rx_size=fake.rx_offset=0;
    pump(&r,8);
    assert(status().timeouts==1 && status().valid_writes==confirmed);
    assert(!status().run_authorized && status().run_locked);
    memcpy(fake.rx,delayed,delayed_size); fake.rx_size=delayed_size; fake.rx_offset=0;
    step(&r,true,true);
    assert(status().late_replies==1 && status().valid_writes==confirmed);
    printf("PASS 10min constant 50Hz, max TX gap=%lu ms, process ACKs distinguished from reads/timeouts\n",
           (unsigned long)max_tx_gap);
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
    r.permissive = true; /* Normal OFF1 keeps the operational VFD-enable relay permissive. */
    r.output = 100;
    run_until_command(&r);
    assert(fake.stw == 0x047e && fake.hsw == 0);
    assert(!V20Modbus_StopConfirmed(stopped_us, .1f)); /* Speed still nonzero. */
    fake.actual = 0;
    for (unsigned i=0; i<40 && !V20Modbus_StopConfirmed(stopped_us,.1f); ++i)
        step(&r,true,true);
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
    const uint16_t bad[] = {0x0201, 0x0209, 0x0241, 0x0001, 0x0200};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
    {
        boot();
        MotorRequest r = request(false, 0);
        fake.status = bad[i];
        pump(&r, 30);
        assert(!status().ready && !status().operational_ready && !V20Modbus_Arm());
        if (bad[i] & 8)
            assert(!status().drive_fault_free && status().communication_ok);
        if (bad[i] == 0x0201)
            assert(status().off2_active && status().off3_active);
        fake.status = 0x0231;
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
    fake.chunk = 2; /* Fragmented frame, but completed within the 80 ms RTU timeout. */
    pump(&r, 60);
    assert(status().ok && status().actual_hz == 0);
    puts("PASS unhealthy drive status, signed speed and fragmented replies");
}
#endif

static void test_inhibited_status_is_communication_healthy(void)
{
    reset_mock();
    fake.status = 0xEB40;
    assert(V20Modbus_Init());
    V20Modbus_Disarm();
    MotorRequest r = request(false, 0);
    pump(&r, 12);
    MotorStatus current = status();
    assert(current.communication_ok && current.response_received && current.status_valid);
    assert(current.drive_fault_free && !current.operational_ready && !current.ready);
    assert(!current.ready_to_switch_on && !current.ready_to_operate &&
           current.off2_active && current.off3_active && current.switch_on_inhibited);
    assert(current.status == 0xEB40 && current.actual_hz == 0);
    assert(!V20Modbus_Arm() && fake.run_commands == 0);
#if !V20_RUN_COMMISSIONED
    assert(fake.commands == 0);
#endif
}

static void test_nonrunning_status_transitions(void)
{
    boot();
    MotorRequest r = request(false, 0);
    fake.status = 0x0233;
    pump(&r, 16);
    MotorStatus current = status();
    assert(current.ready_to_switch_on && current.ready_to_operate &&
           !current.operation_enabled && current.operational_ready);
    assert(!current.run_authorized && fake.run_commands == 0);

    fake.status = 0x0237;
    pump(&r, 16);
    current = status();
    assert(current.ready_to_switch_on && current.ready_to_operate &&
           current.operation_enabled && !current.run_authorized);
    assert(fake.run_commands == 0);
}

static void test_initial_status_exception_stays_read_only(void)
{
    reset_mock();
    fake.damage = 3;
    assert(V20Modbus_Init());
    V20Modbus_Disarm();
    MotorRequest r = request(false, 0);
    pump(&r, 24);
    MotorStatus current = status();
    assert(current.diagnostics_complete && current.status_valid);
    assert(current.last_error == V20_COMM_MODBUS_EXCEPTION);
    assert(fake.good_status_responses > 0);
#if !V20_RUN_COMMISSIONED
    assert(fake.commands == 0);
#endif
}

static void test_initial_status_timeout_retries_read_only(void)
{
    reset_mock();
    fake.damage = 1;
    assert(V20Modbus_Init());
    V20Modbus_Disarm();
    MotorRequest r = request(false, 0);
    pump(&r, 40);
    MotorStatus current = status();
    assert(current.diagnostics_complete && current.status_valid);
    assert(current.errors > 0 && current.timeouts > 0);
    assert(fake.good_status_responses > 0);
#if !V20_RUN_COMMISSIONED
    assert(fake.commands == 0);
#endif
}

static void test_bounded_read_retries(void)
{
    for (unsigned damage = 1; damage <= 5; damage += 4)
    {
        boot(); MotorRequest r = request(false, 0);
        /* Damage exactly one status read, including applied/lost response. */
        for (unsigned i=0; i<50; ++i)
        {
            if (status().transaction.pending && status().transaction.type == V20_OP_READ_STATUS) break;
            step(&r,true,true);
        }
        assert(status().transaction.pending && status().transaction.type == V20_OP_READ_STATUS);
        if (damage == 1) fake.rx_size = fake.rx_offset = 0;
        else fake.rx[fake.rx_size-1] ^= 1;
        unsigned runs = fake.run_commands;
        pump(&r,45);
        MotorStatus m=status();
        assert(m.retries==1 && m.recovered_transactions==1 && m.final_comm_errors==0);
        assert(!m.run_authorized && !m.run_locked && fake.run_commands==runs);
    }
#if V20_RUN_COMMISSIONED
    /* A running read can recover without replaying a captured control word. */
    boot(); MotorRequest running=request(true,50);assert(V20Modbus_Arm());
    while (!(status().transaction.pending && status().transaction.type==V20_OP_READ_STATUS))
        step(&running,true,true);
    fake.rx_size=fake.rx_offset=0;
    pump(&running,45);
    assert(status().retries==1 && status().recovered_transactions==1 && !status().run_locked);
    assert(status().max_process_data_tx_gap_ms < 500);
    /* Noise during quarantine cannot defer the fault beyond the watchdog budget. */
    boot();running=request(true,50);assert(V20Modbus_Arm());
    while (!(status().transaction.pending && status().transaction.type==V20_OP_READ_STATUS))
        step(&running,true,true);
    fake.rx_size=fake.rx_offset=0; pump(&running,8);
    for (unsigned i=0;i<40 && !status().run_locked;++i)
    {
        fake.rx[0]=0xff;fake.rx_size=1;fake.rx_offset=0;
        step(&running,true,true);
    }
    assert(status().run_locked && !V20Modbus_Arm());
    assert(now_us - (int64_t)status().last_process_data_tx_ms*1000 < 450000);
    /* Fail both attempts, then recover wire health: the fault stays latched. */
    boot(); MotorRequest r=request(false,0);
    while (!(status().transaction.pending && status().transaction.type==V20_OP_READ_STATUS))
        step(&r,true,true);
    fake.fail_read=true; pump(&r,45);
    MotorStatus m=status();assert(m.run_locked && m.final_comm_errors>0 && m.retries==1);
    fake.fail_read=false; r.run=true; pump(&r,80);
    assert(status().run_locked && !V20Modbus_Arm() && fake.run_commands==0);
    /* STOP, E-stop and stale safety during read quarantine cannot replay RUN. */
    for (unsigned cause=0;cause<3;++cause)
    {
        boot(); r=request(true,50);assert(V20Modbus_Arm());
        while (!(status().transaction.pending && status().transaction.type==V20_OP_READ_STATUS))
            step(&r,true,true);
        fake.rx_size=fake.rx_offset=0;
        pump(&r,8);unsigned runs=fake.run_commands;
        if (cause==0) {r.run=false;V20Modbus_Disarm();}
        for (unsigned i=0;i<60;i++) step(&r,cause!=2,cause!=1);
        assert(fake.run_commands==runs && !status().run_authorized);
    }
#endif
    puts("PASS single read timeout/CRC recovery counters, bounded outage, STOP/E-stop/stale-request cancellation, no retry START");
}

///////////////////////////////////////////////////////////////////////////////
// Test entry point

int main(void)
{
    test_bounded_read_retries();
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
        MotorStatus diagnostic = status();
        assert(diagnostic.communication_ok && diagnostic.errors == 0);
        if (mismatch == 0)
        {
            assert(!diagnostic.reference_config_valid);
            assert(diagnostic.last_error == V20_COMM_UNEXPECTED_REGISTER_VALUE);
            assert(diagnostic.last_error_register == 40016);
            assert(diagnostic.last_error_expected == 5000);
            assert(diagnostic.last_error_actual == 6000);
        }
        else
            assert(diagnostic.watchdog_valid && !diagnostic.watchdog_config_known &&
                   diagnostic.last_error != V20_COMM_UNEXPECTED_REGISTER_VALUE);
        if (mismatch == 0)
            assert(!V20Modbus_Arm());
        else
            assert(V20Modbus_Arm() == (V20_RUN_COMMISSIONED != 0));
        assert(fake.run_commands == 0);
    }
    puts("PASS UART initialization errors, CRC and read-only configuration diagnostics");
#if V20_RUN_COMMISSIONED
    test_inhibited_status_is_communication_healthy();
    test_nonrunning_status_transitions();
    test_initial_status_exception_stays_read_only();
    test_initial_status_timeout_retries_read_only();
    test_nonrunning_wait_heartbeat();
    test_eb31_start_handshake();
    test_cyclic_controlled_stop();
    test_transaction_timing_and_uart_errors();
    test_constant_process_data();
    test_run_stop();
    test_errors();
    test_interlocks();
    test_status();
#else
    boot();
    assert(status().ready && !status().run_authorized && !V20Modbus_Arm());
    test_inhibited_status_is_communication_healthy();
    test_nonrunning_status_transitions();
    test_initial_status_exception_stays_read_only();
    test_initial_status_timeout_retries_read_only();
    MotorRequest r = request(true, 100);
    pump(&r, 60);
    assert(fake.run_commands == 0 && fake.commands == 0);
    puts("PASS default uncommissioned firmware cannot arm or transmit RUN");
#endif
    return 0;
}
