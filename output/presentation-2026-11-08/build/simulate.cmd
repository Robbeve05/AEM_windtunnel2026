@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "C:\Software\AEM_windtunnel2026\output\presentation-2026-11-08"
cl /nologo /W4 /WX /std:c11 /Ireproduction\firmware_snapshot\include reproduction\compare_controllers.c reproduction\firmware_snapshot\src\Regelaar.c reproduction\firmware_snapshot\src\FrequencyModel.c reproduction\firmware_snapshot\src\TorqueModel.c reproduction\firmware_snapshot\src\SetpointCurve.c /Fobuild\ /Fe:build\compare_controllers.exe
if errorlevel 1 exit /b 1
build\compare_controllers.exe > plots\simulation.csv 2> plots\parameters.json
exit /b %errorlevel%
