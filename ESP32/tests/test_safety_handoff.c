/* Author: R.E. van Eekelen. Concurrent safety publication and real Control_ReadInputs. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define main original_control_suite
#include "test_control.c"
#undef main

static CRITICAL_SECTION queue_mutex;
static volatile LONG producer_done;
void HostQueueLock(void) { EnterCriticalSection(&queue_mutex); }
void HostQueueUnlock(void) { LeaveCriticalSection(&queue_mutex); }
static DWORD WINAPI publish_inputs(LPVOID ignored)
{
    (void)ignored;
    for (unsigned i=1;i<=20000;i++) {
        SafetySnapshot s=SafetyIO_Read();
        s.snapshot_sequence=i;
        s.raw_levels=i;
        s.start_sequence=i;
        s.reset_sequence=~i;
        xQueueOverwrite(safety_status,&s);
    }
    InterlockedExchange(&producer_done,1); return 0;
}
int main(void)
{
    InitializeCriticalSection(&queue_mutex);
    const unsigned sizes[]={sizeof(PiCommand),sizeof(PitotSample),sizeof(MotorRequest),sizeof(MotorStatus),
        sizeof(Telemetry),sizeof(SafetySnapshot),sizeof(PiHeartbeat),sizeof(PressureTapSample)};
    for(unsigned i=0;i<8;i++)q[i].size=sizes[i];
    fake_us=10000000;SafetyIO_Init();
    SafetySnapshot initial=healthy();initial.tick_ms=10000;initial.reset_sequence=UINT32_MAX;
    xQueueOverwrite(safety_status,&initial);
    ControlContext c;healthy_context(&c);
    HANDLE thread=CreateThread(NULL,0,publish_inputs,NULL,0,NULL);assert(thread);
    unsigned reads=0;
    do {
        Control_ReadInputs(&c);reads++;
        assert(c.safety.raw_levels==c.safety.snapshot_sequence);
        assert(c.safety.start_sequence==c.safety.snapshot_sequence);
        assert(c.safety.reset_sequence==~c.safety.snapshot_sequence);
        assert(c.now_ms==10000 && c.safety.tick_ms==10000);
    } while(!InterlockedCompareExchange(&producer_done,0,0));
    assert(WaitForSingleObject(thread,5000)==WAIT_OBJECT_0);CloseHandle(thread);
    DeleteCriticalSection(&queue_mutex);
    printf("PASS 20000 concurrent SafetyIO publications / %u real Control_ReadInputs coherent reads (queue contract modeled)\n",reads);
    return 0;
}
