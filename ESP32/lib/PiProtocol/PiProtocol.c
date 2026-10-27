#include "PiProtocol.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

bool PiProtocol_Parse(const char *s, PiCommand *out)
{
    if (!s || !out) return false;
    static const struct { const char *name; CommandType type; int count; } commands[] = {
        {"STOP", CMD_STOP, 0}, {"START", CMD_START, 0},
        {"SETPOINT", CMD_SETPOINT, 1}, {"PID", CMD_PID, 3}, {"PING", CMD_PING, 0}, {"FILTER", CMD_FILTER, 1}
    };
    while (isspace((unsigned char)*s)) ++s;
    for (unsigned i=0; i<sizeof(commands)/sizeof(commands[0]); ++i) {
        size_t n = strlen(commands[i].name);
        if (strncmp(s, commands[i].name, n) || (s[n] && !isspace((unsigned char)s[n]))) continue;
        PiCommand c = {.type=commands[i].type, .values={0}};
        const char *p = s+n;
        for (int j=0; j<commands[i].count; ++j) {
            if (!isspace((unsigned char)*p)) return false;
            char *end;
            c.values[j] = strtof(p, &end);
            if (end == p || !isfinite(c.values[j])) return false;
            p = end;
        }
        while (isspace((unsigned char)*p)) ++p;
        if (*p) return false;
        *out = c;
        return true;
    }
    return false;
}
