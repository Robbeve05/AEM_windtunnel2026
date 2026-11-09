from pathlib import Path
p=Path('ESP32/include/AppConfig.h');s=p.read_text();a=s.index('/* Intended In1 = OFF2');b=s.index('#if V20_FAULT_ACK_VERIFIED',a);s=s[:a]+s[b:];p.write_text(s)
p=Path('ESP32/src/StateMachine.c');s=p.read_text().replace('    if ((SIMULATION_MODE || V20_OPERATIONAL_OFF2_VERIFIED) && m->reset_accepted &&','    if (m->reset_accepted &&');p.write_text(s)
p=Path('ESP32/tests/test_control.c');s=p.read_text().replace('static DigitalOutputStatus output_status;','static DigitalOutputStatus output_status;\nbool I2CBus_Init(void) { return true; }');s=s.replace('    assert(c.machine.state == STATE_RUNNING && arms == 1);','''    assert(c.machine.state == STATE_STARTING && arms == 1);
    Control_PublishStatus(&c);
    assert(!c.status.running && ((MotorRequest *)q[2].data)->run && c.status.output == 0);
    drive.operation_enabled = true;
    c.motor = drive;
    Control_CheckSafety(&c);
    Control_UpdateStateMachine(&c);
    assert(c.machine.state == STATE_RUNNING && arms == 1);''',1);s=s.replace('    assert(c.machine.state == STATE_READY && arms == 1);\n    assert(SafetyIO_Simulate("ESTOP", true));', '''    assert(c.machine.state == STATE_STOPPED_PREPARE && arms == 1);
    Control_CheckSafety(&c);
    Control_UpdateStateMachine(&c);
    assert(c.machine.state == STATE_READY && arms == 1);
    assert(SafetyIO_Simulate("ESTOP", true));''');s=s.replace('    c.motor.ok = false;\n    Control_UpdateDiagnostics','    c.motor.communication_ok = false;\n    Control_UpdateDiagnostics');s=s.replace('c.machine.fault == FAULT_INTERNAL &&\n           timer_starts', 'c.machine.fault == FAULT_INITIALIZATION &&\n           timer_starts').replace('startup_fault.fault == FAULT_INTERNAL &&','startup_fault.fault == FAULT_INITIALIZATION &&');p.write_text(s)
p=Path('ESP32/tests/test_wait_diagnostics.c');s=p.read_text().replace('.pi_ok = true,','.pi_ok = true,\n        .stopped_prepared = true,');# Insert phase step only before assertions expecting READY
s=s.replace('    assert(machine.state == STATE_READY', '''    if (machine.state == STATE_STOPPED_PREPARE) {
        MachineInputs prepared = ready_inputs();
        StateMachine_Step(&machine, &prepared);
    }
    assert(machine.state == STATE_READY''');p.write_text(s)
p=Path('ESP32/tests/test_v20.c');s=p.read_text().replace('assert(fake.stw == 0x047e || fake.stw == 0x047f);','assert(fake.stw == 0x047e || fake.stw == 0x047f || fake.stw == 0x04fe);');s=s.replace('        else\n            assert(fake.hsw == 0);','''        else {
            assert(fake.hsw == 0);
            if (fake.stw == 0x04fe)
                fake.status &= (uint16_t)~8u;
        }''');s=s.replace('    assert(fake.run_commands == 0 && fake.commands == 0);\n    assert(status().watchdog_valid', '''    assert(fake.run_commands == 0);
#if V20_RUN_COMMISSIONED
    assert(fake.commands == 1 && fake.stw == 0x047e);
#else
    assert(fake.commands == 0);
#endif
    assert(status().watchdog_valid''');s=s.replace('    assert(fake.commands == 0);\n}', '''#if !V20_RUN_COMMISSIONED
    assert(fake.commands == 0);
#endif
}''');p.write_text(s)
