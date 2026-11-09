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
    check(STATE_STOPPED_PREPARE,FAULT_NONE,false,999,false,true,false);
    check(STATE_STARTING,FAULT_NONE,false,0,true,false,false);
    check(STATE_STARTING,FAULT_NONE,false,499,true,false,false);
    check(STATE_STARTING,FAULT_NONE,false,500,false,false,false);
    check(STATE_STARTING,FAULT_NONE,false,999,false,false,false);
    check(STATE_STARTING,FAULT_NONE,false,1000,true,false,false);
    check(STATE_STARTING,FAULT_NONE,true,0,false,false,true);
    assert(strcmp(StatusIndicators_Get(STATE_STARTING,FAULT_NONE,false,false,0).text,
                  "STARTING - WAIT FOR VFD")==0);
    Telemetry starting={.state=STATE_STARTING};
    StatusIndicator starting_on=StatusOutput_Get(&starting,true,499);
    StatusIndicator starting_off=StatusOutput_Get(&starting,true,500);
    assert(starting_on.green && !starting_on.amber && !starting_on.red);
    assert(!starting_off.green && !starting_off.amber && !starting_off.red);
    check(STATE_RUNNING,FAULT_NONE,false,0,true,false,false);
    check(STATE_RUNNING,FAULT_NONE,false,999,true,false,false);
    check(STATE_STOP,FAULT_NONE,false,999,false,true,false);
    const FaultReason_t faults[]={FAULT_SENSOR,FAULT_INTERLOCK,FAULT_MODBUS,FAULT_EMERGENCY_STOP};
    const unsigned times[]={0,499,500,999,1000};
    for (unsigned i=0;i<sizeof(faults)/sizeof(faults[0]);++i) {
        for (unsigned j=0;j<sizeof(times)/sizeof(times[0]);++j) {
            bool on=(times[j]%1000u)<500u;
            check(STATE_FAULT,faults[i],false,times[j],false,false,on);
            check(STATE_FAULT,faults[i],true,times[j],false,false,true);
            Telemetry fault={.state=STATE_FAULT,.fault=faults[i]};
            StatusIndicator shared=StatusOutput_Get(&fault,true,times[j]);
            assert(!shared.green && !shared.amber && shared.red==on);
            if (faults[i]!=FAULT_EMERGENCY_STOP)
                assert(strcmp(shared.text,FaultReason_Name(faults[i]))==0);
        }
    }
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
    puts("PASS INIT/WAIT/STOPPED_PREPARE/READY/STARTING/RUNNING/STOP/FAULT, emergency press/release, blink boundaries, shared telemetry, missing telemetry, existing text");
    return 0;
}
