#include "StateMachine.h"
#include "AppConfig.h"
#include <assert.h>
#include <stdio.h>

static MachineInputs ready_inputs(void)
{
    return (MachineInputs){
        .now_ms = 100,
        .safety = {
            .valid = true,
            .commissioned = true,
            .relay_ok = true,
            .stop_ok = true,
            .outputs_ok = true,
            .tick_ms = 100,
        },
        .sensor_ok = true,
        .drive_ready = true,
        .drive_ok = true,
        .drive_fault_free = true,
        .drive_communication_ok = true,
        .v20_response_received = true,
        .v20_status_healthy = true,
        .v20_modbus_initialized = true,
        .v20_run_commissioned = true,
        .v20_diagnostics_complete = true,
        .v20_configuration_valid = true,
        .pi_ok = true,
    };
}

static void check_reason(MachineInputs inputs, WaitBlockingReason reason)
{
    StateMachine machine = {.state = STATE_WAIT, .reset_accepted = true};
    assert(StateMachine_GetBlockingReasons(&machine, &inputs) == (uint32_t)reason);
}

int main(void)
{
    StateMachine machine = {.state = STATE_WAIT, .reset_accepted = true};
    MachineInputs inputs = ready_inputs();
    assert(StateMachine_GetBlockingReasons(&machine, &inputs) == 0);
    StateMachine_Step(&machine, &inputs);
    assert(machine.state == STATE_READY);

    inputs = ready_inputs();
    inputs.safety.estop_pressed = true;
    check_reason(inputs, WAIT_BLOCK_ESTOP_PRESSED);
    inputs = ready_inputs();
    inputs.safety.stop_ok = false;
    check_reason(inputs, WAIT_BLOCK_STOP_INPUT);
    inputs = ready_inputs();
    inputs.safety.valid = false;
    check_reason(inputs, WAIT_BLOCK_SAFETY_INPUT_INVALID);
    inputs = ready_inputs();
    inputs.now_ms += INPUT_TIMEOUT_MS;
    check_reason(inputs, WAIT_BLOCK_SAFETY_INPUT_STALE);
    inputs = ready_inputs();
    inputs.safety.commissioned = false;
    check_reason(inputs, WAIT_BLOCK_SAFETY_NOT_COMMISSIONED);
    inputs = ready_inputs();
    inputs.safety.relay_ok = false;
    check_reason(inputs, WAIT_BLOCK_SAFETY_RELAY);
    inputs = ready_inputs();
    inputs.safety.outputs_ok = false;
    check_reason(inputs, WAIT_BLOCK_SAFETY_OUTPUTS);
    inputs = ready_inputs();
    inputs.safety.vfd_fault_available = true;
    inputs.safety.vfd_fault = true;
    check_reason(inputs, WAIT_BLOCK_VFD_FAULT);
    inputs = ready_inputs();
    inputs.safety.contactor_available = true;
    inputs.safety.contactor_ok = false;
    check_reason(inputs, WAIT_BLOCK_CONTACTOR);
    inputs = ready_inputs();
    inputs.sensor_ok = false;
    check_reason(inputs, WAIT_BLOCK_SENSOR);
    inputs = ready_inputs();
    inputs.v20_modbus_initialized = false;
    check_reason(inputs, WAIT_BLOCK_MODBUS_NOT_INITIALIZED);
    inputs = ready_inputs();
    inputs.v20_response_received = false;
    check_reason(inputs, WAIT_BLOCK_MODBUS_NO_RESPONSE);
    inputs = ready_inputs();
    inputs.v20_status_healthy = false;
    inputs.drive_ready = false;
    inputs.drive_fault_free = false;
    check_reason(inputs, WAIT_BLOCK_DRIVE_FAULT);
    inputs = ready_inputs();
    inputs.drive_communication_ok = false;
    check_reason(inputs, WAIT_BLOCK_DRIVE_COMMUNICATION);
    inputs = ready_inputs();
    inputs.drive_ready = false;
    check_reason(inputs, WAIT_BLOCK_V20_OPERATION_INHIBITED);
    inputs = ready_inputs();
    inputs.v20_configuration_valid = false;
    check_reason(inputs, WAIT_BLOCK_V20_CONFIGURATION);
    inputs = ready_inputs();
    inputs.v20_run_commissioned = false;
    check_reason(inputs, WAIT_BLOCK_V20_RUN_LOCK);
    inputs = ready_inputs();
    inputs.pi_ok = false;
    check_reason(inputs, WAIT_BLOCK_PI);
    inputs = ready_inputs();
    machine = (StateMachine){.state = STATE_WAIT};
    assert(StateMachine_GetBlockingReasons(&machine, &inputs) == WAIT_BLOCK_RESET_SEQUENCE);
    machine.reset_pending = true;
    assert(StateMachine_GetBlockingReasons(&machine, &inputs) == WAIT_BLOCK_RESET_FEEDBACK_PENDING);

    inputs = ready_inputs();
    inputs.safety.stop_ok = false;
    inputs.safety.commissioned = false;
    inputs.safety.relay_ok = false;
    inputs.safety.outputs_ok = false;
    inputs.sensor_ok = false;
    inputs.v20_modbus_initialized = false;
    inputs.v20_response_received = false;
    inputs.pi_ok = false;
    machine = (StateMachine){.state = STATE_WAIT};
    const uint32_t multiple = StateMachine_GetBlockingReasons(&machine, &inputs);
    assert((multiple & WAIT_BLOCK_STOP_INPUT) &&
           (multiple & WAIT_BLOCK_SAFETY_NOT_COMMISSIONED) &&
           (multiple & WAIT_BLOCK_SAFETY_RELAY) && (multiple & WAIT_BLOCK_SAFETY_OUTPUTS) &&
           (multiple & WAIT_BLOCK_SENSOR) && (multiple & WAIT_BLOCK_MODBUS_NOT_INITIALIZED) &&
           (multiple & WAIT_BLOCK_MODBUS_NO_RESPONSE) && (multiple & WAIT_BLOCK_PI) &&
           (multiple & WAIT_BLOCK_RESET_SEQUENCE));

    inputs = ready_inputs();
    inputs.v20_run_commissioned = false;
    inputs.drive_ready = false;
    machine = (StateMachine){.state = STATE_WAIT, .reset_accepted = true};
    const uint32_t locked = StateMachine_GetBlockingReasons(&machine, &inputs);
    assert(inputs.drive_communication_ok && inputs.v20_status_healthy);
    assert((locked & WAIT_BLOCK_V20_RUN_LOCK) && !(locked & WAIT_BLOCK_DRIVE_COMMUNICATION) &&
           !(locked & WAIT_BLOCK_MODBUS_NOT_INITIALIZED) &&
           !(locked & WAIT_BLOCK_MODBUS_NO_RESPONSE) &&
           (locked & WAIT_BLOCK_V20_OPERATION_INHIBITED));

    MachineInputs supplied_diagnostics = ready_inputs();
    supplied_diagnostics.safety.commissioned = false;
    supplied_diagnostics.drive_ready = false;
    supplied_diagnostics.v20_run_commissioned = false;
    machine = (StateMachine){.state = STATE_WAIT};
    const uint32_t supplied_blockers =
        StateMachine_GetBlockingReasons(&machine, &supplied_diagnostics);
    assert(supplied_blockers == 0x00228010u);
    assert((supplied_blockers & WAIT_BLOCK_V20_OPERATION_INHIBITED) &&
           (supplied_blockers & WAIT_BLOCK_RESET_SEQUENCE) &&
           (supplied_blockers & WAIT_BLOCK_V20_RUN_LOCK) &&
           (supplied_blockers & WAIT_BLOCK_SAFETY_NOT_COMMISSIONED));

    machine.state = STATE_READY;
    assert(StateMachine_GetBlockingReasons(&machine, &inputs) == 0);

    MachineInputs inhibited = ready_inputs();
    inhibited.safety.commissioned = false;
    inhibited.drive_ready = false;
    inhibited.v20_run_commissioned = false;
    inhibited.v20_configuration_valid = false;
    machine = (StateMachine){.state = STATE_INIT, .boot_ms = 0};
    StateMachine_Step(&machine, &inhibited);
    assert(machine.state == STATE_WAIT && machine.fault == FAULT_NONE);
    const uint32_t init_blockers = StateMachine_GetBlockingReasons(&machine, &inhibited);
    assert((init_blockers & WAIT_BLOCK_SAFETY_NOT_COMMISSIONED) &&
           (init_blockers & WAIT_BLOCK_V20_RUN_LOCK) &&
           (init_blockers & WAIT_BLOCK_V20_CONFIGURATION) &&
           (init_blockers & WAIT_BLOCK_V20_OPERATION_INHIBITED));

    machine = (StateMachine){.state = STATE_INIT, .boot_ms = 0};
    inhibited.v20_response_received = false;
    inhibited.v20_diagnostics_complete = false;
    inhibited.now_ms = INIT_TIMEOUT_MS;
    StateMachine_Step(&machine, &inhibited);
    assert(machine.state == STATE_INIT && machine.fault == FAULT_NONE);

    MachineInputs startup = ready_inputs();
    machine = (StateMachine){0};
    StateMachine_Init(&machine, 0);
    StateMachine_Step(&machine, &startup);
    assert(machine.state == STATE_WAIT && !machine.reset_accepted);
    startup.now_ms++;
    StateMachine_Step(&machine, &startup);
    assert(machine.state == STATE_WAIT && !machine.reset_accepted);
    startup.reset = true;
    startup.now_ms++;
    StateMachine_Step(&machine, &startup);
    assert(machine.state == STATE_WAIT && machine.reset_pending);
    startup.reset = false;
    startup.now_ms++;
    StateMachine_Step(&machine, &startup);
    assert(machine.state == STATE_READY && machine.reset_accepted);

    MachineInputs emergency_reset = ready_inputs();
    emergency_reset.sensor_ok = false;
    emergency_reset.reset = true;
    machine = (StateMachine){.state = STATE_FAULT, .fault = FAULT_EMERGENCY_STOP};
    StateMachine_Step(&machine, &emergency_reset);
    assert(machine.state == STATE_WAIT && machine.fault == FAULT_NONE &&
           machine.reset_accepted && !machine.reset_pending);
    emergency_reset.sensor_ok = true;
    emergency_reset.reset = false;
    emergency_reset.now_ms++;
    StateMachine_Step(&machine, &emergency_reset);
    assert(machine.state == STATE_READY && machine.reset_accepted);

    MachineInputs new_fault = ready_inputs();
    new_fault.urgent_fault = FAULT_VFD;
    machine = (StateMachine){.state = STATE_FAULT, .fault = FAULT_EMERGENCY_STOP,
                             .reset_pending = true, .reset_accepted = true};
    StateMachine_Step(&machine, &new_fault);
    assert(machine.state == STATE_FAULT && machine.reset_pending == false &&
           machine.reset_accepted == false);
    new_fault.reset = true;
    new_fault.now_ms++;
    StateMachine_Step(&machine, &new_fault);
    assert(machine.state == STATE_FAULT && !machine.reset_pending &&
           !machine.reset_accepted);
    new_fault.reset = false;
    new_fault.urgent_fault = FAULT_NONE;
    new_fault.now_ms++;
    StateMachine_Step(&machine, &new_fault);
    assert(machine.state == STATE_FAULT && machine.fault == FAULT_EMERGENCY_STOP);
    new_fault.reset = true;
    new_fault.now_ms++;
    StateMachine_Step(&machine, &new_fault);
    assert(machine.state == STATE_WAIT && machine.fault == FAULT_NONE &&
           machine.reset_accepted);
    new_fault.reset = false;
    new_fault.now_ms++;
    StateMachine_Step(&machine, &new_fault);
    assert(machine.state == STATE_READY && machine.fault == FAULT_NONE &&
           machine.reset_accepted);

    puts("PASS WAIT readiness blocker diagnostics and unchanged READY transition");
    return 0;
}
