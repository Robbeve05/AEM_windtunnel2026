# Author: R.E. van Eekelen
# Compile current production sources; host mocks never enter the firmware build.
param([string[]]$Case = @())

$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$install = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $install) { throw 'Visual Studio C++ build tools are required for the host tests.' }
$vcvars = Join-Path $install 'VC\Auxiliary\Build\vcvars64.bat'
$output = Join-Path $project '.pio\host-tests'
New-Item -ItemType Directory -Force -Path $output | Out-Null

$cases = @(
    @{ Name = 'i2c_concurrency'; Sources = @('tests\test_i2c_concurrency.c', 'src\I2CBus.c', 'src\DigitalOutputs.c'); Flags = '/DHOST_OUTPUT_LOCKS /DSAFETY_IO_COMMISSIONED=1' },
    @{ Name = 'i2c100'; Sources = @('tests\test_i2c.c', 'src\I2CBus.c'); Flags = '/DSENSOR_I2C_HZ=100000' },
    @{ Name = 'i2c'; Sources = @('tests\test_i2c.c', 'src\I2CBus.c'); Flags = '' },
    @{ Name = 'main_diagnostics'; Sources = @('tests\test_main_diagnostics.c', 'src\FaultSnapshot.c', 'src\StateMachine.c', 'src\StatusIndicators.c'); Flags = '' },
    @{ Name = "fan_calibration"; Sources = @("tests\test_fan_calibration.c"); Flags = "" },
    @{ Name = 'indicators'; Sources = @('tests\test_indicators.c', 'src\StatusIndicators.c', 'src\StateMachine.c'); Flags = '' },
    @{ Name = 'wait_diagnostics'; Sources = @('tests\test_wait_diagnostics.c', 'src\StateMachine.c'); Flags = '' },
    @{ Name = 'outputs_default'; Sources = @('tests\test_outputs.c', 'src\DigitalOutputs.c'); Flags = '/DHOST_OUTPUT_LOCKS /DSAFETY_IO_COMMISSIONED=0' },
    @{ Name = 'outputs_disabled'; Sources = @('tests\test_outputs.c', 'src\DigitalOutputs.c'); Flags = '/DHOST_OUTPUT_LOCKS /DSAFETY_IO_COMMISSIONED=1 /DSAFETY_RELAY_OK_ACTIVE_LEVEL=1 /DESTOP_PRESSED_ACTIVE_LEVEL=1 /DSTOP_BUTTON_OK_ACTIVE_LEVEL=1 /DSTART_BUTTON_ACTIVE_LEVEL=1 /DRESET_BUTTON_ACTIVE_LEVEL=1' },
    @{ Name = 'outputs_enabled'; Sources = @('tests\test_outputs.c', 'src\DigitalOutputs.c'); Flags = '/DHOST_OUTPUT_LOCKS /DSAFETY_IO_COMMISSIONED=1 /DSAFETY_RELAY_OK_ACTIVE_LEVEL=1 /DESTOP_PRESSED_ACTIVE_LEVEL=1 /DSTOP_BUTTON_OK_ACTIVE_LEVEL=1 /DSTART_BUTTON_ACTIVE_LEVEL=1 /DRESET_BUTTON_ACTIVE_LEVEL=1 /DEXTERNAL_STATUS_LEDS_ENABLED=1' },
    @{ Name = 'outputs_active_low'; Sources = @('tests\test_outputs.c', 'src\DigitalOutputs.c'); Flags = '/DHOST_OUTPUT_LOCKS /DSAFETY_IO_COMMISSIONED=1 /DSAFETY_RELAY_OK_ACTIVE_LEVEL=1 /DESTOP_PRESSED_ACTIVE_LEVEL=1 /DSTOP_BUTTON_OK_ACTIVE_LEVEL=1 /DSTART_BUTTON_ACTIVE_LEVEL=1 /DRESET_BUTTON_ACTIVE_LEVEL=1 /DEXTERNAL_STATUS_LEDS_ENABLED=1 /DSTATUS_LED_ACTIVE_LEVEL=0 /DVFD_ENABLE_ACTIVE_LEVEL=0' },
    @{ Name = 'ota_safety'; Sources = @('tests\test_ota_safety.c'); Flags = '' },
    @{ Name = 'sensors'; Sources = @('tests\test_sensors.c', 'src\PressureSensors.c'); Flags = '/DPRESSURE_TAPS_ENABLED=1 /DABP2_ADDRESS=0x29 /DABP2_MIN_PA=0 /DABP2_MAX_PA=1000 /DABP2_OUTPUT_MIN=1677722 /DABP2_OUTPUT_MAX=15099494' },
    @{ Name = 'core'; Sources = @('tests\test_core.c', 'src\PiProtocol.c'); Flags = '' },
    @{ Name = 'regulator'; Sources = @('tests\test_regelaar.c', 'src\Regelaar.c'); Flags = '' },
    @{ Name = 'models'; Sources = @('tests\test_models.c', 'tests\host\calibration_stub.c', 'src\Regelaar.c', 'src\TorqueModel.c', 'src\FrequencyModel.c'); Flags = '' },
    @{ Name = 'motor_model'; Sources = @('tests\test_torque_inverse.c', 'src\TorqueModel.c', 'tests\host\calibration_stub.c'); Flags = '' },
    @{ Name = 'torque_design'; Sources = @('tests\test_torque_design.c', 'src\TorqueModel.c', 'tests\host\calibration_stub.c'); Flags = '' },
    @{ Name = 'curve'; Sources = @('tests\test_curve.c', 'src\SetpointCurve.c'); Flags = '' },
    @{ Name = 'v20_scheduler'; Sources = @('tests\test_v20_scheduler.c', 'src\V20Modbus.c'); Flags = '/DV20_RUN_COMMISSIONED=1' },
    @{ Name = 'v20'; Sources = @('tests\test_v20.c', 'src\V20Modbus.c'); Flags = '/DV20_RUN_COMMISSIONED=1' },
    @{ Name = 'v20_inhibited'; Sources = @('tests\test_v20.c', 'src\V20Modbus.c'); Flags = '/DV20_RUN_COMMISSIONED=0' },
    @{ Name = 'v20_recovery'; Sources = @('tests\test_v20_recovery.c', 'src\V20Modbus.c', 'src\V20Simulation.c', 'src\StateMachine.c'); Flags = '/DV20_RUN_COMMISSIONED=1' },
    @{ Name = 'v20_fault_ack'; Sources = @('tests\test_v20_recovery.c', 'src\V20Modbus.c', 'src\V20Simulation.c', 'src\StateMachine.c'); Flags = '/DV20_RUN_COMMISSIONED=1 /DV20_FAULT_ACK_VERIFIED=1' },
    @{ Name = 'v20_simulation'; Sources = @('tests\test_v20_simulation.c', 'src\V20Modbus.c', 'src\V20Simulation.c', 'src\StateMachine.c'); Flags = '/DSIMULATION_MODE=1 /DV20_FAULT_ACK_VERIFIED=1' },
    @{ Name = 'startup'; Sources = @('tests\test_startup.c', 'src\StatusIndicators.c', 'src\StateMachine.c'); Flags = '/DSIMULATION_MODE=1' },
    @{ Name = 'control'; Sources = @('tests\test_control.c', 'src\StateMachine.c', 'src\SafetyIO.c', 'src\StatusIndicators.c', 'src\TorqueModel.c', 'src\Regelaar.c', 'src\SetpointCurve.c', 'src\FaultSnapshot.c'); Flags = '/DSIMULATION_MODE=1 /DCONTROL_TYPE=0' },
    @{ Name = 'control_frequency'; Sources = @('tests\test_control.c', 'src\StateMachine.c', 'src\SafetyIO.c', 'src\StatusIndicators.c', 'src\FrequencyModel.c', 'src\Regelaar.c', 'src\SetpointCurve.c', 'src\FaultSnapshot.c'); Flags = '/DSIMULATION_MODE=1 /DCONTROL_TYPE=1' }
)
if ($Case.Count) {
    $unknown = @($Case | Where-Object { $_ -notin $cases.Name })
    if ($unknown.Count) { throw ('Unknown test cases: ' + ($unknown -join ', ')) }
    $cases = @($cases | Where-Object { $_.Name -in $Case })
}
$failed = @()
foreach ($testCase in $cases) {
    $sources = ($testCase.Sources | ForEach-Object { '"' + (Join-Path $project $_) + '"' }) -join ' '
    $lines = @('@echo off', ('call "' + $vcvars + '" >nul'), 'if errorlevel 1 exit /b 1',
               ('pushd "' + $output + '"'), 'if errorlevel 1 exit /b 1')
    $lines += 'cl /nologo /W4 /WX /std:c11 /I"' + (Join-Path $project 'tests\host') + '" /I"' +
              (Join-Path $project 'include') + '" ' + $testCase.Flags + ' ' + $sources +
              ' /Fe:' + $testCase.Name + '_tests.exe'
    $lines += 'if errorlevel 1 exit /b 1'
    $lines += $testCase.Name + '_tests.exe'
    $lines += 'exit /b %errorlevel%'
    $batch = Join-Path $output ($testCase.Name + '.cmd')
    $lines | Set-Content -LiteralPath $batch -Encoding ascii
    & cmd.exe /d /s /c ('call "' + $batch + '"')
    if ($LASTEXITCODE -ne 0) {
        $failed += $testCase.Name
        Write-Host ('FAIL ' + $testCase.Name + ' (compile or execution; exit ' + $LASTEXITCODE + ')')
    } else {
        Write-Host ('PASS executable ' + $testCase.Name)
    }
}
Write-Host (($cases.Count - $failed.Count).ToString() + '/' + $cases.Count + ' host executables passed.')
if ($failed.Count) { throw ('Host regression tests failed or could not execute: ' + ($failed -join ', ')) }
