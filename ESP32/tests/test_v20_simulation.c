#include "AppConfig.h"
#include "V20Modbus.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static int64_t clock_us = 1000000;
static float plant_output;
int64_t esp_timer_get_time(void) { return clock_us; }
TickType_t xTaskGetTickCount(void) { return (TickType_t)(clock_us / 1000); }
void Simulation_SetOutput(float pct) { plant_output=pct; }

static MotorStatus snapshot(void)
{
    MotorStatus s; V20Modbus_GetStatus(&s); return s;
}

static void cycle(StateMachine *m, bool reset, bool start, bool stop)
{
    clock_us += 10000;
    MotorStatus s=snapshot();
    MachineInputs i={.now_ms=(uint32_t)(clock_us/1000),
        .safety={.valid=true,.commissioned=true,.relay_ok=true,.stop_ok=true,.outputs_ok=true,
                 .tick_ms=(uint32_t)(clock_us/1000)},
        .sensor_ok=true,.pi_ok=true,.drive_ready=s.operational_ready,.drive_ok=s.ok,
        .drive_fault_free=s.drive_fault_free,.drive_fault=s.status_valid && (s.status&8u),
        .drive_communication_ok=s.communication_ok,.drive_run_locked=s.run_locked,
        .drive_operation_enabled=s.operation_enabled,.stop_command_acknowledged=s.stopped_command_acknowledged,
        .v20_response_received=s.status_valid,.v20_modbus_initialized=s.initialized,
        .v20_diagnostics_complete=s.diagnostics_complete,.v20_configuration_valid=s.reference_config_valid,
        .v20_run_commissioned=true,.reset=reset,.start=start,.stop=stop,
        .stopped_prepared=V20Modbus_StopConfirmed((int64_t)m->entered_ms*1000,.1f),
        .stop_confirmed=V20Modbus_StopConfirmed((int64_t)m->entered_ms*1000,.1f),
        .start_confirmed=V20Modbus_StartConfirmed((int64_t)m->entered_ms*1000)};
    SystemState_t before=m->state;
    StateMachine_Step(m,&i);
    if (m->recovery_requested) { V20Modbus_RequestRecheck(); (void)V20Modbus_RequestFaultAck(); }
    if (before != STATE_STOPPED_PREPARE && m->state==STATE_STOPPED_PREPARE) V20Modbus_Disarm();
    if (before==STATE_READY && m->state==STATE_STARTING) assert(V20Modbus_Arm());
    if ((before==STATE_RUNNING || before==STATE_STARTING) &&
        m->state!=STATE_RUNNING && m->state!=STATE_STARTING) V20Modbus_Disarm();
    MotorRequest r={.tick=xTaskGetTickCount(),.epoch=m->epoch,.permissive=m->permissive,
        .run=m->state==STATE_STARTING || m->state==STATE_RUNNING,
        .output=m->state==STATE_RUNNING?20.0f:0.0f};
    V20Modbus_Service(&r,true);
}

static void pump_machine(StateMachine *m,unsigned n)
{
    for(unsigned j=0;j<n;j++) cycle(m,false,false,false);
}

int main(void)
{
    StateMachine m; StateMachine_Init(&m,1000);
    assert(V20Modbus_Init());
    assert(V20Modbus_Simulate("V20_RELAY_REQUIRED",true));
    pump_machine(&m,100);
    assert(m.state==STATE_WAIT && !m.permissive && snapshot().off2_active && plant_output==0);
    cycle(&m,true,false,false); pump_machine(&m,100);
    assert(m.state==STATE_READY && m.permissive && !snapshot().operation_enabled && plant_output==0);
    pump_machine(&m,20); assert(m.state==STATE_READY); /* RESET/relay restoration never starts. */
    cycle(&m,false,true,false); assert(m.state==STATE_STARTING);
    pump_machine(&m,100); assert(m.state==STATE_RUNNING && snapshot().operation_enabled);
    assert(plant_output>0);
    cycle(&m,false,false,true); pump_machine(&m,350);
    assert(m.state==STATE_READY && m.permissive && !snapshot().operation_enabled);
    assert(fabsf(snapshot().actual_hz)<=.1f);
    assert(V20Modbus_Simulate("V20_OFF3",true)); pump_machine(&m,30);
    assert(m.state==STATE_FAULT && m.fault==FAULT_DRIVE_INHIBIT);
    assert(V20Modbus_Simulate("V20_OFF3",false));
    pump_machine(&m,30); assert(m.state==STATE_FAULT);
    cycle(&m,true,false,false); pump_machine(&m,100);
    assert(m.state==STATE_READY && !snapshot().run_authorized);
    assert(V20Modbus_Simulate("V20_FAULT",true)); pump_machine(&m,30);
    assert(m.state==STATE_FAULT && m.fault==FAULT_VFD);
    cycle(&m,true,false,false); pump_machine(&m,100);
    assert(m.state==STATE_READY && !snapshot().run_authorized && snapshot().drive_fault_free);
    assert(V20Modbus_Simulate("V20_OFF1",true));
    cycle(&m,false,true,false); pump_machine(&m,520);
    assert(m.state==STATE_FAULT && m.fault==FAULT_START_TIMEOUT);
    /* Reboot with an old RUN request cannot restore software arming. */
    assert(V20Modbus_Init());
    MotorRequest old={.run=true,.permissive=true,.output=50};
    for(unsigned j=0;j<100;j++) {
        clock_us+=10000; old.tick=xTaskGetTickCount(); V20Modbus_Service(&old,true);
    }
    assert(!snapshot().run_authorized && plant_output==0);
    puts("PASS real driver + state machine + relay-dependent simulated V20: boot/reset/prepare/start/enabled/stop, independent OFF1/OFF3, drive fault ACK and reboot");
    return 0;
}
