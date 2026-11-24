#pragma once
#include "V20Modbus.h"
void ModbusTask(void *argument);
bool ModbusTask_CreateInterfaces(void);
void ModbusTask_GetStatus(MotorStatus *status);
bool ModbusTask_Arm(uint32_t epoch);
void ModbusTask_Disarm(void);
void ModbusTask_Reset(void);
bool ModbusTask_StartConfirmed(int64_t entered_us);
bool ModbusTask_StopConfirmed(int64_t entered_us, float maximum_hz);
