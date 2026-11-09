from pathlib import Path
p=Path('ESP32/src/V20Modbus.c');s=p.read_text();s=s.replace('    bool uart_ok;','    bool uart_ok;\n    bool transport_error;\n    unsigned stop_attempts;\n    unsigned fault_ack_phase; /* 0 idle, 1 pulse high requested, 2 low required */\n    int64_t last_stop_ack_us, last_run_ack_us, status_request_us;');s=s.replace('    d.phase = PH_RECOVERY;\n    d.bus_quiet_until_us = now_us + V20_FRAME_GAP_US;', '''    d.transport_error = true;
    d.fault_ack_phase = 0;
    d.phase = PH_RECOVERY;
    d.bus_quiet_until_us = now_us + (int64_t)V20_LATE_REPLY_QUARANTINE_MS * US_PER_MS;''');s=s.replace('    d.last_rx_success_us = now_us;','    d.transport_error = false;\n    d.last_rx_success_us = now_us;');s=s.replace('        uint16_t stw = run ? V20_WORD_RUN : V20_WORD_OFF1;', '''        uint16_t stw = run ? V20_WORD_RUN : V20_WORD_OFF1;
        if (!run && d.fault_ack_phase == 1)
            stw |= 0x0080u;
        if (!run)
            d.stop_attempts++;''');s=s.replace('        d.status_word = get_word(&d.rx[3]);','        d.status_request_us = d.tx_started_us;\n        d.status_word = get_word(&d.rx[3]);');s=s.replace('        if (!d.last_run_command)\n            d.pending_stop = false;', '''        if (!d.last_run_command)
        {
            d.pending_stop = false;
            if ((d.active_stw & 0x0080u) != 0)
                d.fault_ack_phase = 2;
            else
            {
                d.fault_ack_phase = 0;
                d.last_stop_ack_us = now_us;
            }
        }
        else
            d.last_run_ack_us = now_us;''');s=s.replace('    d.latched_fault = false;\n    d.armed = false;', '''    d.latched_fault = false;
    d.transport_error = false;
    d.last_rx_success_us = 0;
    d.last_stop_ack_us = d.last_run_ack_us = d.status_request_us = 0;
    d.control_word_acknowledged_valid = d.control_word_requested_valid = false;
    d.stop_attempts = 0;
    d.pending_stop = true;
    d.armed = false;''');s=s.replace('    const bool stop_required = d.pending_stop || d.demand_run || d.last_run_command;', '''    const bool stop_required = true;
    if (!d.pending_stop && !d.latched_fault)
        d.stop_attempts = 0;''');s=s.replace('    d.recheck_pending = true;\n}', '''    d.recheck_pending = true;
}

bool V20Modbus_RequestFaultAck(void)
{
    /* This API is called only from the accepted physical RESET path. No
     * parameter write is performed. Configuration permission defaults OFF. */
    if (!(SIMULATION_MODE || V20_RUN_COMMISSIONED) || !V20_FAULT_ACK_VERIFIED ||
        !d.reference_config_valid || !d.status_valid || d.armed || d.demand_run ||
        (d.status_word & V20_STATUS_FAULT_BIT) == 0)
        return false;
    d.fault_ack_phase = 1;
    return true;
}''');s=s.replace('    if (d.phase == PH_RECOVERY)\n    {\n        if (now_us < d.bus_quiet_until_us)\n            return;\n        d.phase = PH_IDLE;\n    }', '''    if (!safety_ok)
        d.fault_ack_phase = 0;
    if (d.phase == PH_RECOVERY)
    {
#if !SIMULATION_MODE
        uint8_t discard[V20_MAX_FRAME];
        const int received = uart_read_bytes(MODBUS_UART_PORT, discard, sizeof(discard), 0);
        if (received > 0)
            d.bus_quiet_until_us = now_us + (int64_t)V20_LATE_REPLY_QUARANTINE_MS * US_PER_MS;
#endif
        if (now_us < d.bus_quiet_until_us)
            return;
        d.phase = PH_IDLE;
    }''');s=s.replace('    if (d.latched_fault || now_us < d.bus_quiet_until_us)\n        return;', '    if (now_us < d.bus_quiet_until_us)\n        return;');s=s.replace('    if (!d.status_valid || now_us - d.last_status_us >= (int64_t)V20_STATUS_PERIOD_MS * US_PER_MS)\n    {\n        (void)send_operation(V20_OP_READ_STATUS, false, 0, now_us);\n        return;\n    }\n    if (d.pending_stop || (!d.demand_run && d.last_run_command))', '''    /* A latched run lock never prevents bounded OFF1 attempts. Fault ACK is
     * also bounded, safety gated, and always has ON=0. STOP outranks polling. */
    if (d.fault_ack_phase != 0 && safety_ok && d.stop_attempts < V20_STOP_MAX_ATTEMPTS)
    {
        (void)send_operation(V20_OP_WRITE_CONTROL, false, 0, now_us);
        return;
    }
    if ((d.pending_stop || (!d.demand_run && d.last_run_command)) &&
        d.stop_attempts < V20_STOP_MAX_ATTEMPTS)''');s=s.replace('    if (d.demand_run)\n    {', '''    if (!d.status_valid || now_us - d.last_status_us >= (int64_t)V20_STATUS_PERIOD_MS * US_PER_MS)
    {
        (void)send_operation(V20_OP_READ_STATUS, false, 0, now_us);
        return;
    }
    if (d.latched_fault)
        return;
    if (d.demand_run)
    {''');s=s.replace('d.uart_ok && communication_recent && !d.latched_fault && !d.recheck_pending;', 'd.uart_ok && communication_recent && !d.transport_error;');s=s.replace('        .ready = d.diagnostics_complete', '''        .run_locked = d.latched_fault || d.recheck_pending,
        .warning_active = recent && (d.status_word & 0x0080u) != 0,
        .fault_ack_pending = d.fault_ack_phase != 0,
        .stop_attempts = d.stop_attempts,
        .ready = d.diagnostics_complete''');s=s.replace('           d.last_status_us > stop_entered_us &&', '''           d.last_stop_ack_us >= stop_entered_us &&
           d.status_request_us > d.last_stop_ack_us &&
           (d.status_word & V20_STATUS_OPERATION_ENABLED_BIT) == 0 &&''');s+='''

bool V20Modbus_StartConfirmed(int64_t start_entered_us)
{
    const int64_t now_us = esp_timer_get_time();
    return d.armed && d.demand_run && !d.latched_fault && !d.recheck_pending &&
           d.last_run_ack_us >= start_entered_us && d.status_request_us > d.last_run_ack_us &&
           d.status_valid && d.drive_ready && d.drive_fault_free &&
           (d.status_word & V20_STATUS_OPERATION_ENABLED_BIT) != 0 &&
           now_us - d.last_status_us < (int64_t)MOTOR_STATUS_TIMEOUT_MS * US_PER_MS;
}
''';# last_run_ack must be FIRST run ack, otherwise repeated RUN makes status perpetually older
s=s.replace('        else\n            d.last_run_ack_us = now_us;', '        else if (d.last_run_ack_us == 0)\n            d.last_run_ack_us = now_us;');s=s.replace('    d.armed = true;\n    return true;', '    d.last_run_ack_us = 0;\n    d.armed = true;\n    return true;');p.write_text(s)
