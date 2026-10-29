# Author: R.E. van Eekelen
# Compile current production sources; host mocks never enter the firmware build.
$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$install = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $install) { throw 'Visual Studio C++ build tools are required for the host tests.' }
$vcvars = Join-Path $install 'VC\Auxiliary\Build\vcvars64.bat'
$output = Join-Path $project '.pio\host-tests'
New-Item -ItemType Directory -Force -Path $output | Out-Null

$cases = @(
    @{ Name = 'core'; Sources = @('tests\test_core.c', 'src\Regelaar.c', 'src\PiProtocol.c'); Flags = '' },
    @{ Name = 'curve'; Sources = @('tests\test_curve.c', 'src\SetpointCurve.c'); Flags = '' },
    @{ Name = 'v20'; Sources = @('tests\test_v20.c', 'src\V20Modbus.c'); Flags = '/DV20_RUN_COMMISSIONED=1' },
    @{ Name = 'v20_inhibited'; Sources = @('tests\test_v20.c', 'src\V20Modbus.c'); Flags = '/DV20_RUN_COMMISSIONED=0' },
    @{ Name = 'startup'; Sources = @('tests\test_startup.c'); Flags = '/DSIMULATION_MODE=1' },
    @{ Name = 'control'; Sources = @('tests\test_control.c', 'src\StateMachine.c', 'src\SafetyIO.c', 'src\Regelaar.c', 'src\SetpointCurve.c'); Flags = '/DSIMULATION_MODE=1' }
)
$lines = @('@echo off', ('call "' + $vcvars + '" >nul'), 'if errorlevel 1 exit /b 1',
           ('pushd "' + $output + '"'), 'if errorlevel 1 exit /b 1')
foreach ($case in $cases) {
    $sources = ($case.Sources | ForEach-Object { '"' + (Join-Path $project $_) + '"' }) -join ' '
    $lines += 'cl /nologo /W4 /WX /std:c11 /I"' + (Join-Path $project 'tests\host') + '" /I"' +
              (Join-Path $project 'include') + '" ' + $case.Flags + ' ' + $sources +
              ' /Fe:' + $case.Name + '_tests.exe'
    $lines += 'if errorlevel 1 exit /b 1'
    $lines += $case.Name + '_tests.exe'
    $lines += 'if errorlevel 1 exit /b 1'
}
$lines += @('popd', 'exit /b 0')
$batch = Join-Path $output 'run-tests.cmd'
$lines | Set-Content -LiteralPath $batch -Encoding ascii
Push-Location $output
try {
    & cmd.exe /d /c run-tests.cmd
    if ($LASTEXITCODE -ne 0) { throw 'Host regression tests failed.' }
} finally { Pop-Location }
