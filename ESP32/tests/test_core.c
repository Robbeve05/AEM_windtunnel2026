#include "Regelaar.h"
#include "PiProtocol.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

int main(void)
{
    PiCommand c;
    assert(PiProtocol_Parse("SETPOINT 12.5", &c) && c.type==CMD_SETPOINT && c.values[0]==12.5f);
    assert(PiProtocol_Parse(" PID 2 1 0 \r", &c) && c.type==CMD_PID);
    assert(PiProtocol_Parse("STOP", &c) && c.type==CMD_STOP);
    assert(PiProtocol_Parse("START", &c) && c.type==CMD_START);
    assert(PiProtocol_Parse("PING", &c) && c.type==CMD_PING);
    const char *bad[] = {"", "START extra", "STARTNOW", "PID 1 2", "PID 1 2 3 4",
        "SETPOINT nan", "SETPOINT inf", "SETPOINT 1e999", "SETPOINT 2junk", "PID 1+2 3", "UNKNOWN"};
    for (unsigned i=0; i<sizeof(bad)/sizeof(bad[0]); ++i) assert(!PiProtocol_Parse(bad[i], &c));
    Regelaar p; float u;
    assert(!Regelaar_Init(&p, 1, 1, 0, 0, 0, 100, 0));
    assert(!Regelaar_Init(&p, NAN, 1, 0, .01f, 0, 100, 0));
    assert(Regelaar_Init(&p, 2, 1, 0, .01f, 0, 100, .05f));
    for (int i=0; i<10000; ++i) assert(Regelaar_Step(&p, 1000, 0, &u) && u==100);
    assert(p.integral==0); /* Verzadiging laat de integrator niet oplopen. */
    assert(Regelaar_Step(&p, 0, 0, &u) && u==0);
    assert(!Regelaar_Step(&p, NAN, 0, &u) && u==0);
    assert(Regelaar_Init(&p, 0, 0, 1, .01f, -1000, 1000, 0));
    assert(Regelaar_Step(&p, 0, 5, &u) && u==0);
    assert(Regelaar_Step(&p, 100, 5, &u) && u==0); /* Geen derivative kick bij setpointstap. */
    assert(Regelaar_Step(&p, 100, 6, &u) && fabsf(u+100)<.001f);
    Regelaar_Reset(&p);
    assert(Regelaar_Step(&p, 100, 6, &u) && u==0);
    assert(Regelaar_Init(&p, 2, 1, 0, .01f, 0, 100, .05f));
    float y=0;
    for (int i=0; i<6000; ++i) {
        assert(Regelaar_Step(&p, 10, y, &u));
        assert(u>=0 && u<=100);
        y += (u*.3f-y)*.01f/.5f;
    }
    assert(fabsf(y-10)<.01f);
    puts("PASS: protocol, invalid input, saturation, anti-windup, derivative, reset, closed-loop simulation");
    return 0;
}
