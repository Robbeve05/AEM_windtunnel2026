/* Author: R.E. van Eekelen. Test existing indication policy and telemetry loss. */
#include "AppConfig.h"
#include "StatusOutput.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static void check(SystemState_t state,FaultReason_t fault,bool pressed,unsigned time,bool green,bool yellow,bool red) {
    StatusIndicator s=StatusIndicators_Get(state,fault,pressed,false,time);
    assert(s.green==green && s.amber==yellow && s.red==red && s.text);
}
int main(void) {
    assert(EXTERNAL_STATUS_LEDS_ENABLED==0 && STATUS_LED_BLINK_PERIOD_MS==1000u);
    check(STATE_INIT,FAULT_NONE,false,0,false,true,true);
    check(STATE_WAIT,FAULT_NONE,false,0,false,true,false);
    check(STATE_WAIT,FAULT_NONE,false,500,false,false,false);
    check(STATE_READY,FAULT_NONE,false,499,true,false,false);
    check(STATE_READY,FAULT_NONE,false,500,false,false,false);
    check(STATE_READY,FAULT_NONE,false,1000,true,false,false);
    check(STATE_RUNNING,FAULT_NONE,false,0,true,false,false);
    check(STATE_RUNNING,FAULT_NONE,false,999,true,false,false);
    check(STATE_STOP,FAULT_NONE,false,999,false,true,false);
    check(STATE_FAULT,FAULT_SENSOR,false,999,false,false,true);
    check(STATE_FAULT,FAULT_EMERGENCY_STOP,true,999,false,false,true);
    check(STATE_READY,FAULT_NONE,true,999,false,false,true);
    check(STATE_FAULT,FAULT_EMERGENCY_STOP,false,0,false,false,true);
    check(STATE_FAULT,FAULT_EMERGENCY_STOP,false,500,false,false,false);
    Telemetry t={.state=STATE_RUNNING};
    StatusIndicator unknown=StatusOutput_Get(&t,false,0);
    assert(unknown.red && !unknown.green && strstr(unknown.text,"UNKNOWN"));
    unknown=StatusOutput_Get(NULL,true,0);assert(unknown.red && !unknown.green);
    assert(StatusOutput_Get(&t,true,0).green);
    assert(strstr(StatusIndicators_Get(STATE_FAULT,FAULT_EMERGENCY_STOP,false,false,0).text,"RESET REQUIRED"));
    puts("PASS INIT/WAIT/READY/RUNNING/STOP/FAULT, emergency press/release, blink boundaries, missing telemetry, existing text");
    return 0;
}
