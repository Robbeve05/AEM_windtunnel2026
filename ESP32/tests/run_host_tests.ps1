$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$install = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $install) { throw 'Installeer Visual Studio C++ build tools voor deze hosttests.' }
$vcvars = Join-Path $install 'VC\Auxiliary\Build\vcvars64.bat'
$output = Join-Path $project '.pio\host-tests'
New-Item -ItemType Directory -Force -Path $output | Out-Null
Push-Location $output
try {
    $command = 'pushd "' + $output + '" && call "' + $vcvars + '" >nul && cl /nologo /W4 /std:c11 /I"' + $project + '\lib\Regelaar" /I"' + $project + '\lib\PiProtocol" "' + $project + '\tests\test_core.c" "' + $project + '\lib\Regelaar\Regelaar.c" "' + $project + '\lib\PiProtocol\PiProtocol.c" /Fe:core_tests.exe && core_tests.exe'
    & cmd.exe /d /s /c $command
    if ($LASTEXITCODE -ne 0) { throw 'Hosttests mislukt.' }
    $command = 'pushd "' + $output + '" && call "' + $vcvars + '" >nul && cl /nologo /W4 /WX /std:c11 /I"' + $project + '\lib\V20Modbus" "' + $project + '\tests\test_v20.c" "' + $project + '\lib\V20Modbus\V20Modbus.c" /Fe:v20_tests.exe && v20_tests.exe'
    & cmd.exe /d /s /c $command
    if ($LASTEXITCODE -ne 0) { throw 'V20 tests mislukt.' }
} finally { Pop-Location }
