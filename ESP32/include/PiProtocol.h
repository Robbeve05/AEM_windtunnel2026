#pragma once
#include <stdbool.h>
typedef enum { MODE_AUTO = 0, MODE_MANUAL } OperatingMode;
typedef enum { CMD_STOP, CMD_START, CMD_SETPOINT, CMD_PID, CMD_PING, CMD_FILTER, CMD_MODE, CMD_FREQ } CommandType;
typedef struct { CommandType type; float values[3]; } PiCommand;
bool PiProtocol_Parse(const char *line, PiCommand *command);
