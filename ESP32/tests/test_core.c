#include "PiProtocol.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    PiCommand c;
    assert(PiProtocol_Parse("SETPOINT 12.5", &c) && c.type==CMD_SETPOINT && c.values[0]==12.5f);
    assert(PiProtocol_Parse(" PID 2 1 0 \r", &c) && c.type==CMD_PID);
    assert(PiProtocol_Parse("STOP", &c) && c.type==CMD_STOP);
    assert(!PiProtocol_Parse("START", &c));
    assert(!PiProtocol_Parse("RESET", &c));
    assert(PiProtocol_Parse("MODE AUTO", &c) && c.type==CMD_MODE && c.values[0]==MODE_AUTO);
    assert(PiProtocol_Parse("MODE MANUAL", &c) && c.type==CMD_MODE && c.values[0]==MODE_MANUAL);
    assert(PiProtocol_Parse("FREQ 50", &c) && c.type==CMD_FREQ && c.values[0]==50);
    const char *frequency_bad[]={"FREQ 50.1","FREQ 50.00000001","FREQ 55","FREQ -1","FREQ nan","FREQ inf","FREQ 1e999","MODE MANUAL extra","MODE BAD"};
    for(unsigned i=0;i<sizeof(frequency_bad)/sizeof(frequency_bad[0]);i++) assert(!PiProtocol_Parse(frequency_bad[i],&c));
    assert(PiProtocol_Parse("PING", &c) && c.type==CMD_PING);
    assert(PiProtocol_Parse("FILTER 2.5", &c) && c.type==CMD_FILTER && c.values[0]==2.5f);
    assert(!PiProtocol_Parse("FILTER nan", &c));
    const char *bad[] = {"", "START extra", "STARTNOW", "PID 1 2", "PID 1 2 3 4",
        "SETPOINT nan", "SETPOINT inf", "SETPOINT 1e999", "SETPOINT 2junk", "PID 1+2 3", "UNKNOWN"};
    for (unsigned i=0; i<sizeof(bad)/sizeof(bad[0]); ++i) assert(!PiProtocol_Parse(bad[i], &c));
    puts("PASS Pi protocol validation, no frequency-domain PI");
    return 0;
}
