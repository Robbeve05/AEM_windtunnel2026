#include "PiProtocol.h"
#include "AppConfig.h"
#include <errno.h>
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

bool PiProtocol_Parse(const char *s, PiCommand *out)
{
    if (!s || !out) return false;
    static const struct { const char *name; CommandType type; int count; } commands[] = {
        {"STOP", CMD_STOP, 0}, {"FREQ", CMD_FREQ, 1},
        {"SETPOINT", CMD_SETPOINT, 1}, {"PID", CMD_PID, 3}, {"PING", CMD_PING, 0}, {"FILTER", CMD_FILTER, 1}
    };
    while (isspace((unsigned char)*s)) ++s;
    if (!strncmp(s, "MODE ", 5)) {
        const char *p = s + 5;
        OperatingMode mode;
        if (!strncmp(p, "AUTO", 4) && (!p[4] || isspace((unsigned char)p[4]))) { mode=MODE_AUTO; p+=4; }
        else if (!strncmp(p, "MANUAL", 6) && (!p[6] || isspace((unsigned char)p[6]))) { mode=MODE_MANUAL; p+=6; }
        else return false;
        while (isspace((unsigned char)*p)) ++p;
        if (*p) return false;
        *out=(PiCommand){.type=CMD_MODE,.values={(float)mode}};
        return true;
    }
    for (unsigned i=0; i<sizeof(commands)/sizeof(commands[0]); ++i) {
        size_t n = strlen(commands[i].name);
        if (strncmp(s, commands[i].name, n) || (s[n] && !isspace((unsigned char)s[n]))) continue;
        PiCommand c = {.type=commands[i].type, .values={0}};
        const char *p = s+n;
        for (int j=0; j<commands[i].count; ++j) {
            if (!isspace((unsigned char)*p)) return false;
            char *end;
            errno = 0;
            const double value = strtod(p, &end);
            if (end == p || errno == ERANGE || !isfinite(value) || value < 0 ||
                (c.type == CMD_FREQ && value > MANUAL_MAX_HZ) ||
                (c.type == CMD_SETPOINT && value > MAX_SETPOINT) ||
                (c.type == CMD_PID && value > 1000)) return false;
            c.values[j] = (float)value;
            p = end;
        }
        while (isspace((unsigned char)*p)) ++p;
        if (*p) return false;
        if ((c.type == CMD_FREQ && c.values[0] > MANUAL_MAX_HZ) ||
            (c.type == CMD_SETPOINT && c.values[0] > MAX_SETPOINT) ||
            (c.type == CMD_FILTER && (c.values[0] < 0.1f || c.values[0] > 10.0f))) return false;
        *out = c;
        return true;
    }
    return false;
}
