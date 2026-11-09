#define main existing_v20_suite_main
#include "../ESP32/tests/test_v20.c"
#undef main
#include "StateMachine.h"

int main(void)
{
    boot();
    MotorRequest r = request(true, 50);
    assert(V20Modbus_Arm());
    run_until_command(&r);
    assert(fake.stw == 0x047f);
    fake.fail_read = true;
    pump(&r, 20);
    assert(!status().communication_ok);
    unsigned writes = fake.commands;
    fake.fail_read = false;
    r.run = false;
    pump(&r, 100);
    assert(fake.commands == writes && fake.stw == 0x047f);
    puts("CONFIRMED: runtime RX failure latches driver; no OFF1 is sent even after transport recovers");

    StateMachine m;
    StateMachine_Init(&m, 1000);
    m.state = STATE_READY;
    MachineInputs i = {.now_ms=1001,
        .safety={.valid=true,.commissioned=true,.relay_ok=true,.stop_ok=true,.outputs_ok=true,.tick_ms=1001},
        .sensor_ok=true,.drive_ready=true,.drive_ok=true,.drive_fault_free=true,
        .drive_communication_ok=true,.v20_configuration_valid=true,.v20_run_commissioned=true,
        .pi_ok=true,.start=true};
    StateMachine_Step(&m,&i);
    assert(m.state == STATE_RUNNING);
    puts("CONFIRMED: READY to RUNNING has no operation-enabled feedback prerequisite");
    StateMachine_Fault(&m,FAULT_EMERGENCY_STOP,1001);
    i.start=false; i.reset=true; i.urgent_fault=FAULT_EMERGENCY_STOP;
    StateMachine_Step(&m,&i);
    assert(!m.reset_pending && !m.recovery_requested && m.state == STATE_FAULT);
    puts("CONFIRMED: released E-stop plus physical RESET cannot initiate recovery while software urgent latch persists");
    return 0;
}
