from pathlib import Path
p=Path('ESP32/src/V20Modbus.c');s=p.read_text().replace('    bool transport_error;','    bool transport_error;\n    bool operation_seen, operation_lost;');s=s.replace('        d.status_valid = true;','''        if (d.armed && d.last_run_ack_us > 0 && d.status_request_us > d.last_run_ack_us)
        {
            if ((d.status_word & V20_STATUS_OPERATION_ENABLED_BIT) != 0)
                d.operation_seen = true;
            else if (d.operation_seen)
            {
                d.operation_lost = true;
                d.armed = d.demand_run = false;
                d.pending_stop = true;
                d.stop_attempts = 0;
            }
        }
        d.status_valid = true;''');s=s.replace('    d.transport_error = false;\n    d.last_rx_success_us = 0;', '    d.transport_error = false;\n    d.operation_seen = d.operation_lost = false;\n    d.last_rx_success_us = 0;');s=s.replace('!d.latched_fault && !d.recheck_pending && d.status_valid &&','!d.latched_fault && !d.operation_lost && !d.recheck_pending && d.status_valid &&');s=s.replace('    d.last_run_ack_us = 0;\n    d.armed = true;','    d.last_run_ack_us = 0;\n    d.operation_seen = false;\n    d.armed = true;');s=s.replace('d.armed && requested_run && safety_ok && d.configured && !d.latched_fault;', 'd.armed && requested_run && safety_ok && d.configured && !d.latched_fault && !d.operation_lost;');s=s.replace('.run_locked = d.latched_fault || d.recheck_pending,','.run_locked = d.latched_fault || d.operation_lost || d.recheck_pending,');p.write_text(s)
p=Path('ESP32/include/AppConfig.h');s=p.read_text().replace('#define CONTROL_TYPE CONTROL_TORQUE_PI','#define CONTROL_TYPE CONTROL_FREQUENCY_PI');p.write_text(s)
p=Path('ESP32/tests/run_host_tests.ps1');s=p.read_text().replace("'src\\TorqueModel.c', 'src\\Regelaar.c', 'src\\SetpointCurve.c'); Flags = '/DSIMULATION_MODE=1'", "'src\\TorqueModel.c', 'src\\Regelaar.c', 'src\\SetpointCurve.c'); Flags = '/DSIMULATION_MODE=1 /DCONTROL_TYPE=0'");p.write_text(s)
