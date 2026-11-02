/* Execute the production state machine, no ESP32 or physical safety claims. */
#include <assert.h>
#include <stdio.h>
#include "StateMachine.h"
#include "AppConfig.h"

static MachineInputs good(void) {
    return (MachineInputs){.now_ms=100,
        .safety={.valid=true,.commissioned=true,.relay_ok=true,.stop_ok=true,
                 .outputs_ok=true,.tick_ms=100},
        .sensor_ok=true,.drive_ready=true,.drive_ok=true,.pi_ok=true};
}
static void step(StateMachine *m, MachineInputs *i) {
    i->now_ms++;
    i->safety.tick_ms=i->now_ms;
    StateMachine_Step(m,i);
}
int main(void) {
    StateMachine m;
    MachineInputs i=good();
    StateMachine_Init(&m,0);
    step(&m,&i); assert(m.state==STATE_WAIT && !m.permissive);
    i.start=true; step(&m,&i); assert(m.state==STATE_WAIT);
    i.start=false; i.reset=true; i.safety.reset_pressed=true;
    step(&m,&i); assert(m.state==STATE_WAIT && m.recovery_requested);
    i.reset=false; step(&m,&i); assert(m.state==STATE_READY && !m.permissive);
    i.start=true; step(&m,&i); assert(m.state==STATE_READY); /* Reset held. */
    i.safety.reset_pressed=false;
    step(&m,&i); assert(m.state==STATE_RUNNING && m.permissive);
    i.start=false; i.stop=true; step(&m,&i);
    assert(m.state==STATE_STOP && m.permissive);
    i.stop=false; i.start=true; step(&m,&i); assert(m.state==STATE_STOP);
    i.stop_confirmed=true; step(&m,&i); assert(m.state==STATE_READY && !m.permissive);
    i.start=false; i.stop_confirmed=false; step(&m,&i); assert(m.state==STATE_READY);

    for (int state=STATE_INIT;state<=STATE_FAULT;state++) {
        for (unsigned trip=0;trip<2;trip++) {
            i=good(); StateMachine_Init(&m,0); m.state=(SystemState_t)state;
            if (trip) i.safety.relay_ok=false; else i.safety.estop_pressed=true;
            step(&m,&i);
            assert(m.state==STATE_FAULT && !m.permissive);
            assert(m.fault==(trip?FAULT_SAFETY_FEEDBACK:FAULT_EMERGENCY_STOP));
            i.safety.relay_ok=true; i.safety.estop_pressed=false; i.start=true;
            step(&m,&i); assert(m.state==STATE_FAULT && !m.permissive);
            i.start=false; i.reset=true; i.safety.reset_pressed=true;
            step(&m,&i); assert(m.state==STATE_FAULT && m.recovery_requested);
            i.reset=false; step(&m,&i); assert(m.state==STATE_WAIT && !m.permissive);
            step(&m,&i); assert(m.state==STATE_READY && !m.permissive);
            step(&m,&i); assert(m.state==STATE_READY);
        }
    }
    for (unsigned nc=0;nc<3;nc++) {
        i=good(); StateMachine_Init(&m,0);
        if(nc==0)i.safety.estop_pressed=true;
        if(nc==1)i.safety.stop_ok=false;
        if(nc==2)i.safety.relay_ok=false;
        step(&m,&i);assert(m.state==STATE_FAULT && !m.permissive);
    }
    i=good();StateMachine_Init(&m,0);i.safety.valid=false;
    step(&m,&i);assert(m.state==STATE_INIT);
    i.now_ms=INIT_TIMEOUT_MS;StateMachine_Step(&m,&i);
    assert(m.state==STATE_FAULT && !m.permissive);
    i=good();StateMachine_Init(&m,0);m.state=STATE_STOP;
    i.now_ms=NORMAL_STOP_TIMEOUT_MS;i.safety.tick_ms=i.now_ms;
    StateMachine_Step(&m,&i);assert(m.state==STATE_FAULT && m.fault==FAULT_VFD);
    for(int state=STATE_READY;state<=STATE_STOP;state++) {
        for(unsigned failure=0;failure<7;failure++){
            i=good();StateMachine_Init(&m,0);m.state=(SystemState_t)state;
            if(failure==0)i.sensor_ok=false;
            if(failure==1)i.drive_ready=false;
            if(failure==2)i.drive_fault=true;
            if(failure==3)i.pi_ok=false;
            if(failure==4)i.safety.outputs_ok=false;
            if(failure==5)i.safety.valid=false;
            if(failure==6)i.timing_fault=FAULT_CLOCK_TIMEOUT;
            step(&m,&i);assert(m.state==STATE_FAULT && !m.permissive);
        }
    }
    puts("PASS all six states, normal transitions, NC INIT validation, faults from every state, RESET recovery without restart, timeout/process faults");
    return 0;
}
