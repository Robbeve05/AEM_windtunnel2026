/* Exercise the real driver with the existing UART fake, including delayed
 * responses. These are operational regression tests, never physical safety tests. */
#define main existing_driver_suite
#include "test_v20.c"
#undef main
#include "V20Simulation.h"
#include "StateMachine.h"

static void test_safe_stop_after_fault(void)
{
    boot();
    MotorRequest r = request(true, 50);
    assert(V20Modbus_Arm());
    run_until_command(&r);
    fake.fail_read = true;
    pump(&r, 20);
    assert(status().run_locked && !V20Modbus_Arm());
    fake.fail_read = false;
    pump(&r, 80);
    assert(fake.stw == 0x047e && status().communication_ok && status().run_locked);
    unsigned runs = fake.run_commands;
    r.epoch++;
    pump(&r, 80);
    assert(fake.run_commands == runs && !V20Modbus_Arm());

    /* Transport never recovers: STOP retries remain finite. */
    boot();
    assert(V20Modbus_Arm());
    r = request(true, 50);
    run_until_command(&r);
    fake.fail_write = true;
    pump(&r, 300);
    assert(status().run_locked && status().stop_attempts == V20_STOP_MAX_ATTEMPTS);
    unsigned attempts = status().stop_attempts;
    pump(&r, 300);
    assert(status().stop_attempts == attempts);
}

static void test_enabled_feedback_and_delayed_reply(void)
{
    /* STOP observed while a RUN echo is outstanding cannot authorize a new RUN. */
    boot();
    MotorRequest inflight = request(true, 50);
    assert(V20Modbus_Arm());
    while (fake.run_commands == 0) step(&inflight, true, true);
    unsigned runs_sent = fake.run_commands;
    V20Modbus_Disarm(); inflight.run=false;
    pump(&inflight, 60);
    assert(fake.run_commands == runs_sent && fake.stw == 0x047e && !status().run_authorized);

    boot();
    MotorRequest r = request(true, 0);
    assert(V20Modbus_Arm());
    int64_t start_us = now_us;
    assert(!V20Modbus_StartConfirmed(start_us));
    run_until_command(&r);
    assert(!V20Modbus_StartConfirmed(start_us));
    fake.status = 0x0237;
    pump(&r, 30);
    assert(V20Modbus_StartConfirmed(start_us));
    fake.status = 0x0233; /* Unexpected drive reboot/loss of enabled state. */
    pump(&r, 30);
    assert(status().run_locked && !V20Modbus_Arm() && fake.stw == 0x047e);

    boot();
    r = request(false, 0);
    /* Leave a response in the UART until after its timeout by fragmenting zero
     * available bytes; inject it during quarantine, never as a new response. */
    fake.damage = 1;
    unsigned reads = fake.reads;
    while (fake.reads == reads) step(&r, true, true);
    pump(&r, 9);
    assert(status().run_locked);
    unsigned errors = status().errors;
    uint8_t late[] = {1, 3, 4, 0x02, 0x37, 0, 0, 0, 0};
    uint16_t crc = V20Modbus_Crc16(late, 7);
    late[7] = (uint8_t)crc; late[8] = (uint8_t)(crc >> 8);
    memcpy(fake.rx, late, sizeof(late)); fake.rx_size = sizeof(late); fake.rx_offset = 0;
    step(&r, true, true);
    assert(fake.rx_offset == fake.rx_size);
    assert(!status().operation_enabled); /* Drained, not decoded. */
    pump(&r, 80);
    assert(status().errors >= errors && status().run_locked && !V20Modbus_Arm());

    /* Stop confirmation needs a read REQUEST after the acknowledged OFF1. */
    V20Modbus_RequestRecheck(); pump(&r, 40);
    int64_t stop_us = now_us;
    V20Modbus_Disarm();
    run_until_command(&r);
    assert(!V20Modbus_StopConfirmed(stop_us, .1f));
    pump(&r, 30);
    assert(V20Modbus_StopConfirmed(stop_us, .1f));
    /* MCU reboot destroys previous arm/ack history; stale RUN intent is inert. */
    assert(V20Modbus_Init());
    r = request(true, 50);
    unsigned before = fake.run_commands;
    pump(&r, 60);
    assert(!status().run_authorized && fake.run_commands == before);
}

static void test_fault_ack_gate(void)
{
    boot();
    fake.status = 0x0239;
    MotorRequest r = request(false, 0);
    pump(&r, 30);
    V20Modbus_RequestRecheck();
    bool accepted = V20Modbus_RequestFaultAck();
    assert(accepted == (V20_FAULT_ACK_VERIFIED != 0));
    unsigned runs = fake.run_commands;
    pump(&r, 80);
    assert(fake.run_commands == runs && (fake.stw & 1u) == 0);
    assert(status().drive_fault_free == (V20_FAULT_ACK_VERIFIED != 0));
    assert(!status().fault_ack_pending);
    if (V20_FAULT_ACK_VERIFIED)
    {
        fake.status = 0x0239; pump(&r, 30);
        assert(V20Modbus_RequestFaultAck());
        step(&r, true, false); /* Safety loss cancels queued acknowledgment. */
        pump(&r, 50);
        assert(!status().drive_fault_free && !status().fault_ack_pending);
    }
}

static MachineInputs inputs(void)
{
    return (MachineInputs){.now_ms=1000,
        .safety={.valid=true,.commissioned=true,.relay_ok=true,.stop_ok=true,
                 .outputs_ok=true,.tick_ms=1000},
        .sensor_ok=true,.pi_ok=true,.drive_ready=true,.drive_ok=true,.drive_fault_free=true,
        .drive_communication_ok=true,.v20_response_received=true,.v20_modbus_initialized=true,
        .v20_diagnostics_complete=true,.v20_configuration_valid=true,.v20_run_commissioned=true,
        .stop_command_acknowledged=true};
}

static void test_state_phases(void)
{
    StateMachine m = {.state=STATE_WAIT};
    MachineInputs i = inputs();
    i.reset=true; StateMachine_Step(&m,&i);
    assert(m.state == STATE_WAIT && m.reset_pending);
    i.reset=false; StateMachine_Step(&m,&i);
    assert(m.state == STATE_STOPPED_PREPARE && m.permissive);
    assert(!i.start_confirmed);
    StateMachine_Step(&m,&i); assert(m.state == STATE_STOPPED_PREPARE);
    i.stopped_prepared=true; StateMachine_Step(&m,&i);
    assert(m.state == STATE_READY && m.permissive);
    i.start=true; StateMachine_Step(&m,&i);
    assert(m.state == STATE_STARTING);
    i.start=false; StateMachine_Step(&m,&i); assert(m.state == STATE_STARTING);
    i.start_confirmed=true; i.drive_operation_enabled=true;
    StateMachine_Step(&m,&i); assert(m.state == STATE_RUNNING);
    i.drive_operation_enabled=false;
    StateMachine_Step(&m,&i); assert(m.fault == FAULT_DRIVE_INHIBIT);
    i = inputs(); m=(StateMachine){.state=STATE_READY};
    i.start=true; StateMachine_Step(&m,&i);
    i.start=false; i.now_ms += V20_START_TIMEOUT_MS; i.safety.tick_ms=i.now_ms;
    StateMachine_Step(&m,&i); assert(m.fault == FAULT_START_TIMEOUT);
    i=inputs(); m=(StateMachine){.state=STATE_STARTING}; i.stop=true;
    StateMachine_Step(&m,&i); assert(m.state == STATE_STOP);
    i=inputs(); m=(StateMachine){.state=STATE_READY}; i.v20_configuration_valid=false;
    StateMachine_Step(&m,&i); assert(m.fault == FAULT_CONFIGURATION);
    i=inputs(); m=(StateMachine){.state=STATE_READY}; i.drive_fault=true;
    StateMachine_Step(&m,&i); assert(m.fault == FAULT_VFD);
    i=inputs(); m=(StateMachine){.state=STATE_READY}; i.drive_communication_ok=false;
    StateMachine_Step(&m,&i); assert(m.fault == FAULT_MODBUS);
}

static void test_model(void)
{
    V20Simulation v; V20Simulation_Init(&v);
    v.relay_required=true; v.relay_permission=false;
    V20Simulation_Step(&v,.01f);
    assert((V20Simulation_Status(&v)&0x50u)==0x40u);
    v.relay_permission=true;
    V20Simulation_Command(&v,0x047e,0); V20Simulation_Step(&v,.01f);
    assert((V20Simulation_Status(&v)&0x71u)==0x31u);
    V20Simulation_Command(&v,0x047f,8192); V20Simulation_Step(&v,1);
    assert(v.enabled && v.actual_hz>0);
    v.off3=true; V20Simulation_Step(&v,1);
    assert(!v.enabled && !(V20Simulation_Status(&v)&0x20u));
    v.off3=false; V20Simulation_Command(&v,0x047f,8192); V20Simulation_Step(&v,1);
    assert(!v.enabled); /* ON is not a fresh edge after inhibit. */
    V20Simulation_Command(&v,0x047e,0);
    v.fault=true; V20Simulation_Command(&v,0x04fe,0);
    assert(!v.fault && !(v.control&1u));
    v.fault=v.persistent_fault=true;
    V20Simulation_Command(&v,0x047e,0); V20Simulation_Command(&v,0x04fe,0);
    assert(v.fault);
    v.fault=v.persistent_fault=false; v.off2=true;
    V20Simulation_Step(&v,.01f); assert(!(V20Simulation_Status(&v)&0x10u));
    V20Simulation_Init(&v); V20Simulation_Command(&v,0x047f,8192);
    V20Simulation_Step(&v,1); float hz=v.actual_hz;
    V20Simulation_Command(&v,0x047e,0); V20Simulation_Step(&v,.1f);
    assert(!v.enabled && v.actual_hz<hz && v.actual_hz>0);
    V20Simulation_Step(&v,10); assert(v.actual_hz==0);
}

int main(void)
{
    test_safe_stop_after_fault();
    test_enabled_feedback_and_delayed_reply();
    test_fault_ack_gate();
    test_state_phases();
    test_model();
    puts("PASS STOP retries/run lock, delayed replies, fresh enabled/stop feedback, reboot, gated fault ACK, phases, independent drive model");
    return 0;
}
