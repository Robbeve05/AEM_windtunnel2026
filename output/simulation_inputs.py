from pathlib import Path
p=Path('ESP32/src/V20Modbus.c');s=p.read_text().replace('static int64_t simulation_previous_us;', '''static int64_t simulation_previous_us;
static portMUX_TYPE simulation_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t simulation_inputs, simulation_previous_inputs;''');s=s.replace('    simulation_previous_us = esp_timer_get_time();','    simulation_inputs = simulation_previous_inputs = 0;\n    simulation_previous_us = esp_timer_get_time();',1);s=s.replace('    simulated_drive.relay_permission = safety_ok && request != NULL && request->permissive;', '''    portENTER_CRITICAL(&simulation_lock);
    uint32_t inputs = simulation_inputs;
    portEXIT_CRITICAL(&simulation_lock);
    simulated_drive.off1 = (inputs & 1u) != 0;
    simulated_drive.off2 = (inputs & 2u) != 0;
    simulated_drive.off3 = (inputs & 4u) != 0;
    simulated_drive.persistent_fault = (inputs & 16u) != 0;
    if (((inputs & 8u) && !(simulation_previous_inputs & 8u)) || simulated_drive.persistent_fault)
        simulated_drive.fault = true;
    simulated_drive.relay_required = (inputs & 32u) != 0;
    simulated_drive.relay_permission = !(inputs & 64u) && safety_ok && request != NULL && request->permissive;
    simulation_previous_inputs = inputs;''');s+='''

bool V20Modbus_Simulate(const char *name, bool active)
{
#if SIMULATION_MODE
    static const char *names[] = {"V20_OFF1", "V20_OFF2", "V20_OFF3", "V20_FAULT",
        "V20_FAULT_PERSISTENT", "V20_RELAY_REQUIRED", "V20_RELAY_BLOCKED"};
    if (name == NULL) return false;
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (strcmp(name, names[i]) == 0)
        {
            portENTER_CRITICAL(&simulation_lock);
            if (active) simulation_inputs |= 1u << i;
            else simulation_inputs &= ~(1u << i);
            portEXIT_CRITICAL(&simulation_lock);
            return true;
        }
#else
    (void)name; (void)active;
#endif
    return false;
}
''';p.write_text(s)
p=Path('ESP32/src/PiCommunicationTask.c');s=p.read_text().replace('reply(SafetyIO_Simulate(name, active != 0) ? "OK SIM\\n" : "ERR SIM\\n");','reply((SafetyIO_Simulate(name, active != 0) || V20Modbus_Simulate(name, active != 0)) ? "OK SIM\\n" : "ERR SIM\\n");');p.write_text(s)
p=Path('ESP32/tests/run_host_tests.ps1');s=p.read_text().replace("    @{ Name = 'startup';", "    @{ Name = 'v20_recovery'; Sources = @('tests\\test_v20_recovery.c', 'src\\V20Modbus.c', 'src\\V20Simulation.c', 'src\\StateMachine.c'); Flags = '/DV20_RUN_COMMISSIONED=1' },\n    @{ Name = 'v20_fault_ack'; Sources = @('tests\\test_v20_recovery.c', 'src\\V20Modbus.c', 'src\\V20Simulation.c', 'src\\StateMachine.c'); Flags = '/DV20_RUN_COMMISSIONED=1 /DV20_FAULT_ACK_VERIFIED=1' },\n    @{ Name = 'startup';");p.write_text(s)
p=Path('ESP32/tests/test_torque_design.c');s=p.read_text().replace(' assert(SAFETY_IO_COMMISSIONED==0);',' assert(SAFETY_IO_COMMISSIONED==1);').replace(' assert(V20_RUN_COMMISSIONED==0);',' assert(V20_RUN_COMMISSIONED==1);');p.write_text(s)
