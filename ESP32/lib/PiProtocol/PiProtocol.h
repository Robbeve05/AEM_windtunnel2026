#pragma once
#include <stdbool.h>
typedef enum { CMD_STOP, CMD_START, CMD_SETPOINT, CMD_PID, CMD_PING } CommandType;
typedef struct { CommandType type; float values[3]; } PiCommand;
bool PiProtocol_Parse(const char *line, PiCommand *command);
