/* Real ControlTask + real PressureSensors, deterministic I2C and UART/RTOS fakes. */
#define main existing_control_test_suite
#include "test_control.c"
#undef main
#include "I2CBus.h"
static uint16_t raw_count=8192;
static uint8_t status_bits;
static bool read_failure;
static uint32_t bus_us=2000;
bool I2CBus_TryReadOnce(uint8_t address,uint8_t *bytes,size_t size) {
    assert(address==PITOT_ADDRESS && size==4);pitot_reads++;fake_us+=bus_us;
    bytes[0]=(uint8_t)((status_bits<<6)|(raw_count>>8));bytes[1]=(uint8_t)raw_count;
    bytes[2]=0x80;bytes[3]=0;return !read_failure;
}
int main(void) {
    const unsigned sizes[]={sizeof(PiCommand),sizeof(PitotSample),sizeof(MotorRequest),sizeof(MotorStatus),sizeof(Telemetry),sizeof(SafetySnapshot),sizeof(PiHeartbeat),sizeof(PressureTapSample)};
    for(unsigned i=0;i<8;i++) q[i].size=sizes[i];
    assert(SensorLib_Init());ControlContext c;healthy_context(&c);fake_us=1000000;
    c.machine.state=STATE_WAIT;
    for(unsigned i=0;i<200;i++) {fake_us+=10000;Control_AcquirePitot(&c);assert(!c.sample.valid);}
    healthy_context(&c);c.machine.state=STATE_RUNNING;c.motor.operation_enabled=true;
    raw_count=8292;unsigned before=pitot_reads;c.started_us=fake_us;
    Control_AcquirePitot(&c);assert(pitot_reads==before+1 && c.sample.valid && c.sample.value>0);
    assert(((PitotSample*)q[1].data)->sequence==c.sample.sequence);
    ((PitotSample*)q[1].data)->value=999; /* Queue poison cannot affect local feedback. */
    float pv=c.sample.value;Control_UpdateController(&c);
    assert(c.controller.previous_measurement==pv && c.controller.previous_measurement!=999);
    fake_modbus_service_us=V20_SERVICE_BUDGET_US-1;
    Control_UpdateMotor(&c);Control_UpdateDiagnostics(&c);
    assert(c.status.pitot_acquisition_us==2000 && c.status.execution_us==6999 && !c.status.overruns);
    for(unsigned failure=0;failure<4;failure++) {
        healthy_context(&c);c.machine.state=STATE_RUNNING;c.controller.integral=12;
        status_bits=failure==0?2:failure==1?3:0;read_failure=failure>=2;
        before=pitot_reads;Control_AcquirePitot(&c);
        assert(pitot_reads==before+1 && !c.sample.valid && !c.status.sensor_ok);
        assert(!((PitotSample*)q[1].data)->valid);
        Control_UpdateController(&c);assert(c.machine.state==STATE_FAULT && c.machine.fault==FAULT_SENSOR);
        assert(!c.controller.previous_valid && c.controller.integral==0);
    }
    read_failure=false;status_bits=0;unsigned previous_arms=arms;
    Control_AcquirePitot(&c);Control_UpdateController(&c);
    assert(c.sample.valid && c.machine.state==STATE_FAULT && arms==previous_arms);
    c.stop=true;before=pitot_reads;uint32_t seq=c.sample.sequence;
    Control_AcquirePitot(&c);assert(pitot_reads==before && c.sample.sequence==seq);
    c.stop=false;c.safety.estop_pressed=true;Control_AcquirePitot(&c);assert(pitot_reads==before);
    /* Same total-cycle guard detects an intentionally over-budget fake. */
    c.safety.estop_pressed=false;c.started_us=fake_us;bus_us=6000;
    Control_AcquirePitot(&c);Control_UpdateMotor(&c);Control_UpdateDiagnostics(&c);
    assert(c.status.execution_us>=10000 && c.status.overruns);
    puts("PASS same-cycle real Pitot -> ISA, no queue feedback, one read, invalid/stale faults, no restart, STOP bypass, modeled 6999us incl Modbus, overrun latch");
    return 0;
}
