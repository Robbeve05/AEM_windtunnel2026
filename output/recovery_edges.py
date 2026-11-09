from pathlib import Path
p=Path('ESP32/src/ControlTask.c');s=p.read_text().replace('(!ctx->motor.communication_ok || ctx->motor.run_locked)', '!ctx->motor.communication_ok');s=s.replace('    bool v20_initialized;','    bool v20_initialized;\n    uint32_t v20_init_retry_ms;');s=s.replace('    const int64_t service_started_us = esp_timer_get_time();\n    if (ctx->v20_initialized)', '''    const int64_t service_started_us = esp_timer_get_time();
    if (!ctx->v20_initialized && ctx->now_ms - ctx->v20_init_retry_ms >= 1000u)
    {
        ctx->v20_init_retry_ms = ctx->now_ms;
        ctx->v20_initialized = V20Modbus_Init();
        V20Modbus_Disarm();
    }
    if (ctx->v20_initialized)''');p.write_text(s)
p=Path('ESP32/src/V20Modbus.c');s=p.read_text();a=s.index('    if (uart_param_config(');b=s.index('    d.uart_ok = true;',a);s=s[:a]+'''    if (uart_param_config(MODBUS_UART_PORT, &cfg) != ESP_OK ||
        uart_set_pin(MODBUS_UART_PORT, MODBUS_TX_PIN, MODBUS_RX_PIN, MODBUS_DE_PIN,
                     UART_PIN_NO_CHANGE) != ESP_OK)
    {
        record_error(V20_COMM_UART_INIT_FAILED, 0, 0);
        return false;
    }
    if (uart_driver_install(MODBUS_UART_PORT, V20_RX_BUFFER, V20_TX_BUFFER, 0, NULL, 0) != ESP_OK)
    {
        record_error(V20_COMM_UART_INIT_FAILED, 0, 0);
        return false;
    }
    if (uart_set_mode(MODBUS_UART_PORT, UART_MODE_RS485_HALF_DUPLEX) != ESP_OK)
    {
        (void)uart_driver_delete(MODBUS_UART_PORT);
        record_error(V20_COMM_UART_INIT_FAILED, 0, 0);
        return false;
    }
'''+s[b:];s=s.replace('    if (!safety_ok)\n        d.fault_ack_phase = 0;', '''    if (!safety_ok && d.fault_ack_phase != 0)
    {
        d.fault_ack_phase = 0;
        d.pending_stop = true;
    }''');p.write_text(s)
p=Path('ESP32/tests/host/driver/uart.h');s=p.read_text().replace('esp_err_t uart_set_mode', 'esp_err_t uart_driver_delete(int port);\nesp_err_t uart_set_mode');p.write_text(s)
p=Path('ESP32/tests/test_v20.c');s=p.read_text().replace('esp_err_t uart_set_mode(int port, int mode)', 'esp_err_t uart_driver_delete(int port) { assert(port == MODBUS_UART_PORT); return ESP_OK; }\n\nesp_err_t uart_set_mode(int port, int mode)');p.write_text(s)
p=Path('ESP32/tests/test_control.c');s=p.read_text();pos=s.rindex('    return 0;');s=s[:pos]+'''    /* Released but still-latched safety must recover through physical RESET,
     * without the test manually calling SafetyIO_Acknowledge first. */
    fake_us = 1000000;
    (void)SafetyIO_Simulate("RELAY", true);
    (void)SafetyIO_Simulate("STOP", false);
    (void)SafetyIO_Simulate("ESTOP", false);
    SafetyIO_Acknowledge();
    healthy_context(&c); c.machine.state=STATE_FAULT; c.machine.fault=FAULT_EMERGENCY_STOP;
    unsigned arms_before=arms;
    assert(SafetyIO_Simulate("ESTOP", true));
    assert(SafetyIO_Simulate("ESTOP", false));
    assert(SafetyIO_Fault()==FAULT_EMERGENCY_STOP);
    c.safety=SafetyIO_Read(); c.reset=false;
    Control_CheckSafety(&c); Control_UpdateStateMachine(&c);
    assert(c.machine.state==STATE_FAULT && SafetyIO_Fault()==FAULT_EMERGENCY_STOP);
    c.reset=true;
    Control_CheckSafety(&c);
    assert(SafetyIO_Fault()==FAULT_NONE);
    Control_UpdateStateMachine(&c);
    assert(c.machine.state==STATE_WAIT && arms==arms_before);
    c.reset=false; Control_CheckSafety(&c); Control_UpdateStateMachine(&c);
    assert(c.machine.state==STATE_STOPPED_PREPARE && arms==arms_before);
    Control_CheckSafety(&c); Control_UpdateStateMachine(&c);
    assert(c.machine.state==STATE_READY && arms==arms_before);
    (void)SafetyIO_Simulate("ESTOP", true); c.safety=SafetyIO_Read(); c.reset=true;
    Control_CheckSafety(&c); Control_UpdateStateMachine(&c);
    assert(SafetyIO_Fault()==FAULT_EMERGENCY_STOP && c.machine.state==STATE_FAULT && arms==arms_before);
    (void)SafetyIO_Simulate("ESTOP", false); SafetyIO_Acknowledge();
    /* Diagnostic milliseconds are independent of the RTOS tick value. */
    healthy_context(&c); c.now_tick=42; c.now_ms=1000; c.safety.tick_ms=950;
    Control_PublishStatus(&c); assert(c.status.ready_diag.safety_inputs_fresh);
    c.safety.tick_ms=900; Control_PublishStatus(&c);
    assert(!c.status.ready_diag.safety_inputs_fresh);
    puts("PASS released safety latch RESET integration, active emergency rejection, no automatic START, millisecond freshness");
'''+s[pos:];p.write_text(s)
