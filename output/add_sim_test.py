from pathlib import Path
p=Path('ESP32/tests/run_host_tests.ps1');s=p.read_text().replace("    @{ Name = 'startup';", "    @{ Name = 'v20_simulation'; Sources = @('tests\\test_v20_simulation.c', 'src\\V20Modbus.c', 'src\\V20Simulation.c', 'src\\StateMachine.c'); Flags = '/DSIMULATION_MODE=1 /DV20_FAULT_ACK_VERIFIED=1' },\n    @{ Name = 'startup';");p.write_text(s)
