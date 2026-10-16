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
    assert(Regelaar_Step(&p, 0, 0, &u) && fabsf(u-5.0f)<.001f);
    assert(!Regelaar_Step(&p, NAN, 0, &u) && u==0);
    assert(Regelaar_Init(&p, 0, 2, 0, .1f, -1000, 1000, .05f));
    assert(Regelaar_Step(&p, 1, 0, &u) && fabsf(u-.1f)<.001f);
    assert(Regelaar_Step(&p, 3, 0, &u) && fabsf(u-.5f)<.001f);
    assert(Regelaar_Init(&p, 0, 0, 1, .01f, -1000, 1000, .05f));
    assert(Regelaar_Step(&p, 0, 5, &u) && u==0);
    assert(Regelaar_Step(&p, 100, 5, &u) && u==0); /* Geen derivative kick bij setpointstap. */
    assert(Regelaar_Step(&p, 100, 6, &u) && fabsf(u+(2.0f/.11f))<.001f);
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
    /* Independent double-precision evaluation of Python's VFD design. */
    const double wc = 2.0 * 3.14159265358979323846 * 0.20;
    const double wi = wc / 10.0;
    const double kp_hz = sqrt(1.0 + pow(wc * 0.6, 2.0)) /
                         (0.8 * sqrt(1.0 + pow(wi / wc, 2.0)));
    const float times[] = {0.001f, 0.05f};
    for (unsigned k = 0; k < 2; ++k) {
        assert(Regelaar_InitWindtunnel(&p, times[k]));
        assert(fabs(p.kp - 2.0 * kp_hz) < 1e-6);
        assert(fabs(p.ki - 2.0 * kp_hz * wi) < 1e-6);
        assert(p.kd == 0 && p.minimum == 0 && p.maximum == 100);
        double previous = 0, expected = 0;
        for (int n = 0; n < 100; ++n) {
            const float error = 1.0f + 0.1f * sinf((float)n);
            expected += (2.0*kp_hz + kp_hz*wi*times[k])*error +
                        (-2.0*kp_hz + kp_hz*wi*times[k])*previous;
            assert(Regelaar_Step(&p, error, 0, &u));
            assert(fabs(u - expected) < 0.0001);
            previous = error;
        }
        Regelaar_Reset(&p);
        assert(p.integral == 0 && !p.initialized);
        for (int n = 0; n < 10000; ++n)
            assert(Regelaar_Step(&p, 1000, 0, &u) && u == 100);
        assert(p.integral == 0);
        Regelaar_Reset(&p);
        float air = 0;
        for (int n = 0; n < (int)(120.0f / times[k]); ++n) {
            assert(Regelaar_Step(&p, 10, air, &u));
            /* VFD proxy: 100% = 50 Hz, 50 Hz = 40 m/s. */
            air += (0.4f*u - air)*times[k]/0.6f;
        }
        assert(fabsf(air - 10.0f) < 0.01f);
    }
    assert(!Regelaar_InitWindtunnel(&p, 0));
    assert(!Regelaar_InitWindtunnel(&p, NAN));
    assert(!Regelaar_InitWindtunnel(&p, 0.5f));
    printf("VFD design: Kp=%.9f Hz/(m/s), Ki=%.9f Hz/m; percent gains x2\n",
           kp_hz, kp_hz*wi);
    puts("PASS: protocol, invalid input, Tustin PI(D), saturation, anti-windup, reset, closed-loop simulation");
    return 0;
}
