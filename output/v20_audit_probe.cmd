@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /W4 /WX /std:c11 /DV20_RUN_COMMISSIONED=1 /IESP32\tests\host /IESP32\include output\v20_audit_probe.c ESP32\src\V20Modbus.c ESP32\src\StateMachine.c /Fooutput\ /Fe:output\v20_audit_probe.exe
if errorlevel 1 exit /b 1
output\v20_audit_probe.exe
