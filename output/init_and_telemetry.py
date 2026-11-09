from pathlib import Path
p=Path('ESP32/tests/test_startup.c');s=p.read_text().replace('static bool calibration_initialized;', 'static bool calibration_initialized;\nstatic bool retry_output;\nstatic unsigned recovery_passes;');s=s.replace('    longjmp(task_finished,1);\n}\nint xQueuePeek', '''    if (retry_output && recovery_passes++ == 0) { output_ok=true; return; }
    longjmp(task_finished,1);
}
int xQueuePeek''');s=s.replace('    if (!setjmp(task_finished)) SensorTask(NULL);\n    assert(*handle_ThreadEventGroup & EVT_SENSOR_TASK_READY);','''    retry_output=true; recovery_passes=0;
    if (!setjmp(task_finished)) InputHandlerTask(NULL);
    assert((*handle_ThreadEventGroup & EVT_INPUT_TASK_READY) && output_ok);
    retry_output=false;
    if (!setjmp(task_finished)) SensorTask(NULL);
    assert(*handle_ThreadEventGroup & EVT_SENSOR_TASK_READY);''');p.write_text(s)
p=Path('ESP32/tests/test_control.c');s=p.read_text();pos=s.rindex('    return 0;');s=s[:pos]+'''    healthy_context(&c); c.machine.state=STATE_FAULT; c.machine.fault=FAULT_INITIALIZATION;
    c.tasks_ready=false; thread_bits=EVT_ALL_TASKS_READY;
    Control_HoldInitializationFault(&c);
    assert(c.tasks_ready && c.machine.state==STATE_INIT && !c.machine.reset_accepted);
    Control_PublishStatus(&c); assert(!((MotorRequest*)q[2].data)->run);
    puts("PASS recovered task barrier resumes INIT without reset acceptance or RUN");
'''+s[pos:];p.write_text(s)
p=Path('ESP32/src/InputHandlerTask.c');s=p.read_text().replace('    bool initialized = false;', '    bool initialized = false;\n    uint32_t last_init_retry_ms = 0;');s=s.replace('        if (!output_state.initialized)\n            (void)I2CBus_Init();', '''        if (!output_state.initialized && s.tick_ms - last_init_retry_ms >= 1000u)
        {
            last_init_retry_ms = s.tick_ms;
            (void)I2CBus_Init();
        }''');p.write_text(s)
p=Path('ESP32/src/ControlTask.c');s=p.read_text().replace('    diag->do1_requested = outputs.vfd_requested;', '    diag->digital_outputs_initialized = outputs.initialized;\n    diag->digital_outputs_healthy = outputs.initialized && outputs.confirmed_valid;\n    diag->do1_requested = outputs.vfd_requested;');p.write_text(s)
p=Path('ESP32/src/PiCommunicationTask.c');s=p.read_text();needle='                for (int tap = 0; tap < 16';idx=s.index(needle);s=s[:idx]+'''                if (length > 0 && length < (int)sizeof(buffer) - 180)
                    length += snprintf(buffer + length, sizeof(buffer) - length,
                        " diag_v20_run_locked=%d diag_v20_warning=%d diag_v20_fault_ack_pending=%d"
                        " diag_v20_stop_attempts=%lu",
                        motor.run_locked, motor.warning_active, motor.fault_ack_pending,
                        (unsigned long)motor.stop_attempts);
'''+s[idx:];p.write_text(s)
p=Path('HMI/backend/esp32.py');s=p.read_text().replace('INTS = {', 'INTS = {"diag_v20_run_locked", "diag_v20_warning", "diag_v20_fault_ack_pending", "diag_v20_stop_attempts", ');p.write_text(s)
