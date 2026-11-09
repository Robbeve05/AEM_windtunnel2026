from pathlib import Path
p=Path('ESP32/src/StateMachine.c');s=p.read_text();s=s.replace('"STOP", "FAULT"','"STOP", "FAULT", "STOPPED_PREPARE", "STARTING"').replace('(unsigned)s < 6','(unsigned)s < 8');s=s.replace('"INTERLOCK", "INTERNAL"','"INTERLOCK", "INTERNAL",\n        "DRIVE_INHIBIT", "CONFIGURATION", "START_TIMEOUT", "INITIALIZATION"');s=s.replace('m->state != STATE_WAIT)', '(m->state != STATE_WAIT && m->state != STATE_STOPPED_PREPARE))');s=s.replace('const bool new_fault = m->state == STATE_FAULT && m->fault != f;','const bool new_fault = m->state == STATE_FAULT && m->fault != f;');s=s.replace('    m->last_fault = f;','    if (m->fault == FAULT_NONE || f == FAULT_EMERGENCY_STOP)\n        m->last_fault = f;');s=s.replace('    if (m->state == STATE_READY || m->state == STATE_RUNNING || m->state == STATE_STOP)','    if (m->state == STATE_READY || m->state == STATE_STARTING ||\n        m->state == STATE_RUNNING || m->state == STATE_STOP || m->state == STATE_STOPPED_PREPARE)');s=s.replace('        else if (!i->drive_ok || !i->drive_ready)\n            StateMachine_Fault(m, FAULT_MODBUS, i->now_ms);','''        else if (!i->drive_communication_ok || i->drive_run_locked)
            StateMachine_Fault(m, FAULT_MODBUS, i->now_ms);
        else if (!i->v20_configuration_valid)
            StateMachine_Fault(m, FAULT_CONFIGURATION, i->now_ms);
        else if ((!i->drive_ready || !i->drive_ok) && m->state != STATE_STOPPED_PREPARE)
            StateMachine_Fault(m, FAULT_DRIVE_INHIBIT, i->now_ms);''');s=s.replace('i->timing_fault == FAULT_NONE && !i->drive_fault &&','i->timing_fault == FAULT_NONE &&');s=s.replace('        (!i->safety.vfd_fault_available || !i->safety.vfd_fault) &&\n        (!i->safety.contactor_available', '        (!i->safety.contactor_available');s=s.replace('i->drive_communication_ok && i->drive_fault_free && i->safety.outputs_ok','i->drive_communication_ok && i->safety.outputs_ok');s=s.replace('            transition(m, STATE_READY, i->now_ms);\n        break;\n    case STATE_READY:', '''            transition(m, STATE_STOPPED_PREPARE, i->now_ms);
        break;
    case STATE_STOPPED_PREPARE:
        /* No relay permission is invented here. D4 requires installation checks. */
        if (i->stop || !i->safety.stop_ok)
        {
            m->reset_accepted = false;
            transition(m, STATE_WAIT, i->now_ms);
        }
        else if (healthy && process && i->stopped_prepared)
            transition(m, STATE_READY, i->now_ms);
        else if (i->now_ms - m->entered_ms >= V20_PREPARE_TIMEOUT_MS)
            StateMachine_Fault(m, FAULT_DRIVE_INHIBIT, i->now_ms);
        break;
    case STATE_READY:''');s=s.replace('            transition(m, STATE_RUNNING, i->now_ms);\n        break;\n    case STATE_RUNNING:', '''            transition(m, STATE_STARTING, i->now_ms);
        break;
    case STATE_STARTING:
        if (i->stop || !i->safety.stop_ok)
            transition(m, STATE_STOP, i->now_ms);
        else if (i->start_confirmed)
            transition(m, STATE_RUNNING, i->now_ms);
        else if (i->now_ms - m->entered_ms >= V20_START_TIMEOUT_MS)
            StateMachine_Fault(m, FAULT_START_TIMEOUT, i->now_ms);
        break;
    case STATE_RUNNING:''');s=s.replace('(m->state == STATE_RUNNING || m->state == STATE_STOP);','(m->state == STATE_STARTING || m->state == STATE_RUNNING || m->state == STATE_STOP);');p.write_text(s)
p=Path('ESP32/src/ControlTask.c');s=p.read_text();s=s.replace('ctx->machine.state != STATE_RUNNING && ctx->machine.state != STATE_STOP','ctx->machine.state != STATE_STARTING && ctx->machine.state != STATE_RUNNING && ctx->machine.state != STATE_STOP');s=s.replace('if (ctx->machine.reset_pending && !ctx->safety.estop_pressed && ctx->safety.relay_ok)\n        SafetyIO_Acknowledge();','''if (ctx->reset && (ctx->machine.state == STATE_WAIT || ctx->machine.state == STATE_FAULT) &&
        ctx->safety.valid && ctx->safety.commissioned && ctx->safety.outputs_ok &&
        ctx->now_ms - ctx->safety.tick_ms < INPUT_TIMEOUT_MS &&
        !ctx->safety.estop_pressed && !ctx->safety.reset_pressed && ctx->safety.stop_ok &&
        ctx->safety.relay_ok &&
        (!ctx->safety.contactor_available || ctx->safety.contactor_ok))
        SafetyIO_Acknowledge();''');s=s.replace('.drive_fault = (ctx->motor.status & 0x0008u) != 0,','.drive_fault = ctx->motor.status_valid && (ctx->motor.status & 0x0008u) != 0,\n                        .drive_run_locked = ctx->motor.run_locked,');s=s.replace('.stop_confirmed = stop_confirmed};','''.stop_confirmed = stop_confirmed,
                        .stopped_prepared = V20Modbus_StopConfirmed(
                            (int64_t)ctx->machine.entered_ms * 1000LL, STOP_CONFIRM_HZ),
                        .start_confirmed = V20Modbus_StartConfirmed(
                            (int64_t)ctx->machine.entered_ms * 1000LL)};''');s=s.replace('    if (ctx->machine.recovery_requested)\n        V20Modbus_RequestRecheck();','''    if (ctx->machine.recovery_requested)
    {
        V20Modbus_RequestRecheck();
        (void)V20Modbus_RequestFaultAck();
    }
    if (previous_state != STATE_STOPPED_PREPARE && ctx->machine.state == STATE_STOPPED_PREPARE)
        V20Modbus_Disarm();''');s=s.replace('ctx->machine.state == STATE_RUNNING)\n    {\n        const bool feedback_ok','ctx->machine.state == STATE_STARTING)\n    {\n        const bool feedback_ok');s=s.replace('(previous_state == STATE_RUNNING && ctx->machine.state != STATE_RUNNING)', '((previous_state == STATE_RUNNING || previous_state == STATE_STARTING) &&\n         ctx->machine.state != STATE_RUNNING && ctx->machine.state != STATE_STARTING)');s=s.replace('(TickType_t)(ctx->now_tick - ctx->safety.tick_ms) < pdMS_TO_TICKS(INPUT_TIMEOUT_MS)','ctx->now_ms - ctx->safety.tick_ms < INPUT_TIMEOUT_MS');s=s.replace('.run = ctx->status.running,','.run = ctx->status.running || ctx->machine.state == STATE_STARTING,');start=s.index('    if (!ctx->v20_initialized ||',s.index('static void Control_UpdateDiagnostics(ControlContext *ctx)\n{'));end=s.index('    ctx->status.overruns',start);s=s[:start]+'''    /* State-specific drive diagnosis belongs to StateMachine_Step. Only transport
     * loss is acted upon here immediately after service; never relabel a VFD trip. */
    if (!ctx->v20_initialized ||
        ((!ctx->motor.communication_ok || ctx->motor.run_locked) &&
         (ctx->machine.state == STATE_STARTING || ctx->machine.state == STATE_RUNNING ||
          ctx->machine.state == STATE_READY || ctx->machine.state == STATE_STOP)))
    {
        StateMachine_Fault(&ctx->machine, FAULT_MODBUS, ctx->now_ms);
        V20Modbus_Disarm();
    }
'''+s[end:];s=s.replace('static void Control_HoldInitializationFault(ControlContext *ctx);','static void Control_HoldInitializationFault(ControlContext *ctx);');s=s.replace('StateMachine_Fault(&ctx->machine, FAULT_INTERNAL, ctx->now_ms);\n    V20Modbus_Disarm();\n    Control_PublishStatus(ctx);','StateMachine_Fault(&ctx->machine, FAULT_INITIALIZATION, ctx->now_ms);\n    V20Modbus_Disarm();\n    Control_PublishStatus(ctx);',1);needle='''        Control_PublishStatus(ctx);
        vTaskDelay(pdMS_TO_TICKS(CONTROL_CLOCK_TIMEOUT_MS));''';s=s.replace(needle,'''        Control_PublishStatus(ctx);
        if ((xEventGroupGetBits(handle_ThreadEventGroup) & EVT_ALL_TASKS_READY) == EVT_ALL_TASKS_READY)
        {
            /* Restored initialization is not reset acceptance or run permission. */
            ctx->tasks_ready = true;
            StateMachine_Init(&ctx->machine, ctx->now_ms);
            ctx->previous_start_sequence = ctx->safety.start_sequence;
            ctx->previous_reset_sequence = ctx->safety.reset_sequence;
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(CONTROL_CLOCK_TIMEOUT_MS));''');p.write_text(s)
p=Path('ESP32/src/InputHandlerTask.c');s=p.read_text();s=s.replace('            initialized = true;\n            if (s.outputs_ok)','            if (s.outputs_ok)').replace('                ESP_LOGI("InputHandler",','                initialized = true;\n                ESP_LOGI("InputHandler",');s=s.replace('        s.outputs_ok = DigitalOutputs_Set(enable, indicators);','''        /* Retry only the inactive initialization path until the module is ready. */
        DigitalOutputStatus output_state = {0};
        DigitalOutputs_GetStatus(&output_state);
        if (!output_state.initialized)
            (void)I2CBus_Init();
        s.outputs_ok = DigitalOutputs_Set(enable, indicators);''');p.write_text(s)
p=Path('ESP32/src/I2CBus.c');s=p.read_text().replace('    mutex = xSemaphoreCreateMutex();','    if (!mutex)\n        mutex = xSemaphoreCreateMutex();').replace('    ready = i2c_param_config','    if (!ready)\n        ready = i2c_param_config').replace('    bool ok = ready && DigitalOutputs_Init();\n    if (!ok)\n        ready = false;\n    return ok;','    /* Keep an installed bus usable when only the inactive output write failed. */\n    return ready && DigitalOutputs_Init();');p.write_text(s)
p=Path('ESP32/src/SensorTask.c');s=p.read_text().replace('    for (;;)\n    {','''    for (;;)
    {
        if (!ready)
        {
            vTaskDelay(pdMS_TO_TICKS(100));
            ready = SensorLib_Init();
            if (ready)
                xEventGroupSetBits(handle_ThreadEventGroup, EVT_SENSOR_TASK_READY);
        }''');p.write_text(s)
p=Path('ESP32/src/StatusIndicators.c');s=p.read_text().replace('    case STATE_READY:','''    case STATE_STOPPED_PREPARE:
        return (StatusIndicator){.amber = true, .text = "VERIFYING STOPPED DRIVE"};
    case STATE_STARTING:
        return (StatusIndicator){.amber = blink, .text = "STARTING - WAIT FOR VFD"};
    case STATE_READY:''');p.write_text(s)
p=Path('ESP32/src/PiCommunicationTask.c');s=p.read_text().replace('(current.state == STATE_RUNNING || current.state == STATE_STOP)', '(current.state == STATE_STARTING || current.state == STATE_RUNNING || current.state == STATE_STOP)');p.write_text(s)
p=Path('HMI/backend/esp32.py');s=p.read_text().replace('"STOP", "FAULT"}', '"STOP", "FAULT", "STOPPED_PREPARE", "STARTING"}');p.write_text(s)
