from pathlib import Path
p=Path('ESP32/src/V20Modbus.c');s=p.read_text().replace('#include "V20Modbus.h"','#include "V20Modbus.h"\n#if SIMULATION_MODE\n#include "V20Simulation.h"\nstatic V20Simulation simulated_drive;\nstatic int64_t simulation_previous_us;\n#endif');s=s.replace('#if SIMULATION_MODE\n    d.uart_ok = true;','#if SIMULATION_MODE\n    V20Simulation_Init(&simulated_drive);\n    simulation_previous_us = esp_timer_get_time();\n    d.uart_ok = true;');a=s.index('        if (d.active_stw == V20_WORD_RUN)',s.index('static void simulate_response(int64_t now_us)\n{'));b=s.index('        d.rx_used = 8;',a);s=s[:a]+'''        V20Simulation_Command(&simulated_drive, d.active_stw, d.active_hsw);
'''+s[b:];s=s.replace('put_word(&d.rx[3], 0x0231u);','put_word(&d.rx[3], V20Simulation_Status(&simulated_drive));').replace('lroundf(d.actual_hz * 16384.0f / V20_REFERENCE_HZ)','lroundf(simulated_drive.actual_hz * 16384.0f / V20_REFERENCE_HZ)');s=s.replace('    const int64_t now_us = esp_timer_get_time();\n    if (!d.uart_ok)', '''    const int64_t now_us = esp_timer_get_time();
#if SIMULATION_MODE
    simulated_drive.relay_permission = safety_ok && request != NULL && request->permissive;
    V20Simulation_Step(&simulated_drive, (float)(now_us - simulation_previous_us) / 1000000.0f);
    simulation_previous_us = now_us;
    Simulation_SetOutput(simulated_drive.actual_hz * 2.0f);
#endif
    if (!d.uart_ok)''');p.write_text(s)
p=Path('ESP32/tests/test_startup.c');s=p.read_text().replace('bool DigitalOutputs_Set(bool enabled, StatusIndicator indicators)', '''void vTaskDelay(TickType_t ticks) { (void)ticks; }
void DigitalOutputs_GetStatus(DigitalOutputStatus *s) { *s=(DigitalOutputStatus){.initialized=true}; }
bool DigitalOutputs_Set(bool enabled, StatusIndicator indicators)''');p.write_text(s)
p=Path('ESP32/tests/test_control.c');s=p.read_text().replace('bool V20Modbus_StopConfirmed(int64_t t, float hz)', '''bool V20Modbus_RequestFaultAck(void) { return false; }
bool V20Modbus_StartConfirmed(int64_t t) { (void)t;return drive.operation_enabled; }
bool V20Modbus_StopConfirmed(int64_t t, float hz)''');p.write_text(s)
