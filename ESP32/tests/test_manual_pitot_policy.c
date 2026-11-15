#define main existing_control_suite
#include "test_control.c"
#undef main
static void setup(ControlContext *c, SystemState_t state, bool manual) {
    healthy_context(c);c->status.mode=manual?MODE_MANUAL:MODE_AUTO;
    c->status.sensor_ok=false;c->sample.valid=false;c->sample.value=NAN;
    c->motor.tick=c->now_tick;c->motor.operation_enabled=state==STATE_RUNNING;
    c->machine.state=state;c->machine.reset_accepted=true;c->status.manual_hz=20;
    Control_CheckSafety(c);
}
int main(void) {
    const unsigned sizes[]={sizeof(PiCommand),sizeof(PitotSample),sizeof(MotorRequest),sizeof(MotorStatus),sizeof(Telemetry),sizeof(SafetySnapshot),sizeof(PiHeartbeat),sizeof(PressureTapSample)};
    for(unsigned n=0;n<8;n++) q[n].size=sizes[n];
    const SystemState_t states[]={STATE_INIT,STATE_WAIT,STATE_STOPPED_PREPARE,STATE_READY,STATE_STARTING,STATE_RUNNING,STATE_STOP};
    for(unsigned manual=0;manual<2;manual++) {
        const bool optional=manual != 0;
        assert(StateMachine_PitotRequired(manual!=0)==!optional);
        for(unsigned n=0;n<sizeof(states)/sizeof(states[0]);n++) {
            ControlContext c;setup(&c,states[n],manual!=0);
            if(states[n]==STATE_WAIT || states[n]==STATE_STOPPED_PREPARE)
                assert(((StateMachine_GetBlockingReasons(&c.machine,&c.inputs)&WAIT_BLOCK_SENSOR)!=0)==!optional);
            StateMachine_Step(&c.machine,&c.inputs);
            if(n<2) assert((c.machine.state!=states[n])==optional);
            else assert((c.machine.fault==FAULT_SENSOR)==!optional);
            assert(!c.status.sensor_ok && !c.sample.valid);
        }
        ControlContext c;setup(&c,STATE_RUNNING,manual!=0);Control_UpdateController(&c);
        assert(!c.controller.previous_valid && c.controller.integral==0);
        assert((c.machine.fault==FAULT_SENSOR)==!optional);
        if(optional) assert(c.frequency_running && isfinite(c.status.output));
        StateMachine_Fault(&c.machine,FAULT_SENSOR,c.now_ms);StateMachine_Step(&c.machine,&c.inputs);
        assert(c.machine.state==STATE_FAULT);
    }
    for(unsigned failure=0;failure<10;failure++) {
        ControlContext c;setup(&c,STATE_RUNNING,true);
        switch(failure) {
        case 0:c.inputs.safety.estop_pressed=true;break;
        case 1:c.inputs.safety.relay_ok=false;break;
        case 2:c.inputs.safety.outputs_ok=false;break;
        case 3:c.inputs.safety.valid=false;break;
        case 4:c.inputs.drive_fault=true;break;
        case 5:c.inputs.drive_communication_ok=false;break;
        case 6:c.inputs.v20_configuration_valid=false;break;
        case 7:c.inputs.drive_run_locked=true;break;
        case 8:c.inputs.pi_ok=false;break;
        case 9:c.inputs.timing_fault=FAULT_CLOCK_TIMEOUT;break;
        }
        StateMachine_Step(&c.machine,&c.inputs);assert(c.machine.state==STATE_FAULT && !c.machine.permissive);
    }
    /* Persistent loss is deliberately diagnostic-only in MANUAL.
     * Selecting AUTO while stopped immediately restores the sensor interlock. */
    {
        ControlContext d;setup(&d,STATE_RUNNING,true);
        for(unsigned n=0;n<50;n++) {
            Control_UpdateController(&d);
            assert(d.machine.state==STATE_RUNNING && !d.controller.previous_valid);
        }
        setup(&d,STATE_READY,true);d.status.mode=MODE_AUTO;
        Control_CheckSafety(&d);Control_UpdateStateMachine(&d);
        assert(d.machine.fault==FAULT_SENSOR);
        setup(&d,STATE_FAULT,true);d.machine.fault=FAULT_SENSOR;
        d.inputs.reset=true;StateMachine_Step(&d.machine,&d.inputs);
        assert(d.machine.state==STATE_WAIT && d.machine.reset_accepted);
    }
    ControlContext c;setup(&c,STATE_RUNNING,true);c.inputs.stop=true;StateMachine_Step(&c.machine,&c.inputs);
    assert(c.machine.state==STATE_STOP);
    setup(&c,STATE_READY,true);c.inputs.sensor_ok=true;StateMachine_Step(&c.machine,&c.inputs);
    assert(c.machine.state==STATE_READY);
    puts("PASS sensor policy across states, invalid ISA guard, interlocks, STOP, physical START required");return 0;
}
