#pragma once
#include <stdbool.h>
typedef enum { CMD_SETPOINT, CMD_PID, CMD_PING, CMD_FILTER, CMD_MODE_AUTO, CMD_MODE_MANUAL, CMD_FREQ } CommandType;
typedef struct { CommandType type; float values[3]; } PiCommand;
bool PiProtocol_Parse(const char *line, PiCommand *command);
