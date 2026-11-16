/* Author: R.E. van Eekelen. Real recorder, bounded wrap-safe safety and concurrent readers. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "FaultRecorder.h"
#include "FaultCodes.h"
#include "FaultCodeRegistry.h"
#include "SafetyTime.h"
#include "freertos/FreeRTOS.h"

static CRITICAL_SECTION mutex;
void Host_EnterCritical(portMUX_TYPE *lock) { (void)lock; EnterCriticalSection(&mutex); }
void Host_ExitCritical(portMUX_TYPE *lock) { (void)lock; LeaveCriticalSection(&mutex); }
static volatile LONG finished;
static DWORD WINAPI writer(LPVOID ignored)
{
    (void)ignored;
    MachineInputs input = {.manual_mode=true,.safety={.valid=true}};
    for (unsigned i=1;i<=10000;i++) {
        input.now_ms=i+100; input.safety.tick_ms=i; input.safety.snapshot_sequence=i;
        FaultRecorder_EndEpisode();
        FaultRecorder_Capture(FAULT_INTERLOCK,STATE_READY,&input,SAFETY_SNAPSHOT_STALE);
    }
    InterlockedExchange(&finished,1); return 0;
}
int main(void)
{
    InitializeCriticalSection(&mutex);
    SafetySnapshot s={.valid=true,.tick_ms=100};
    assert(SafetyTime_Fresh(&s,100) && SafetyTime_Fresh(&s,199));
    assert(!SafetyTime_Fresh(&s,200) && !SafetyTime_Fresh(&s,201));
    assert(!SafetyTime_Fresh(&s,99) && !SafetyTime_AgeValid(99,100));
    s.tick_ms=UINT32_MAX-49; assert(SafetyTime_Fresh(&s,25));
    assert(SafetyTime_Age(25,s.tick_ms)==75);
    s.valid=false; assert(!SafetyTime_Fresh(&s,25));
    unsigned entries=0; uint16_t codes[32];
#define FAULT(reason, code, text) assert(FaultCodes_ForReason(reason)==code); codes[entries++]=code;
#define INTERLOCK(bit, code, text) assert(FaultCodes_ForInterlock(bit)==code); codes[entries++]=code;
    FAULT_CODE_REGISTRY(FAULT,INTERLOCK)
#undef FAULT
#undef INTERLOCK
    for(unsigned i=0;i<entries;i++)for(unsigned j=0;j<i;j++)assert(codes[i]!=codes[j]);
    assert(FaultCodes_KnownInterlocks()==127);
    assert(FaultCodes_ForInterlock(SAFETY_SNAPSHOT_STALE|DIGITAL_OUTPUT_UNHEALTHY)==0x0302);
    FaultRecorder_Reset();
    MachineInputs in={.now_ms=269567,.manual_mode=true,
        .safety={.valid=true,.tick_ms=269568,.snapshot_sequence=42}};
    FaultRecorder_Capture(FAULT_INTERLOCK,STATE_READY,&in,SAFETY_SNAPSHOT_STALE);
    FaultRecord first,record; bool active; uint32_t lost;
    assert(FaultRecorder_First(&first,&active) && active && first.age_ms==UINT32_MAX);
    assert(first.code==0x0302 && !(first.flags&FAULT_RECORD_AGE_VALID) && first.previous_state==STATE_READY);
    in.now_ms++; FaultRecorder_Capture(FAULT_INTERLOCK,STATE_FAULT,&in,0);
    assert(FaultRecorder_Count()==1);
    FaultRecorder_Capture(FAULT_EMERGENCY_STOP,STATE_FAULT,&in,0);
    assert(FaultRecorder_Count()==2 && FaultRecorder_First(&record,&active));
    assert(!memcmp(&first,&record,sizeof(first)));
    FaultRecorder_EndEpisode(); assert(FaultRecorder_First(&record,&active) && !active);
    for(unsigned i=0;i<40;i++) {
        FaultRecorder_EndEpisode(); in.now_ms++;
        FaultRecorder_Capture(FAULT_SENSOR,STATE_RUNNING,&in,0);
    }
    assert(FaultRecorder_Count()==32);
    assert(FaultRecorder_ReadAfter(0,&record,&lost) && record.sequence==11 && lost==10);
    char text[256]; assert(FaultRecorder_Format(&first,text,sizeof(text))>0);
    assert(strstr(text,"code=0302") && strstr(text,"age=4294967295"));
    assert(FaultRecorder_Format(&first,text,10)<0);
    FaultRecorder_Reset();
    HANDLE thread=CreateThread(NULL,0,writer,NULL,0,NULL); assert(thread);
    uint32_t cursor=0;
    do {
        if(FaultRecorder_ReadAfter(cursor,&record,&lost)) {
            assert(record.now_ms==record.safety_tick_ms+100);
            assert(record.safety_sequence==record.safety_tick_ms && record.age_ms==100);
            assert(record.code==0x0302); cursor=record.sequence;
        }
    } while(!InterlockedCompareExchange(&finished,0,0));
    assert(WaitForSingleObject(thread,5000)==WAIT_OBJECT_0); CloseHandle(thread);
    DeleteCriticalSection(&mutex);
    puts("PASS fresh/boundary/stale/future/wrap; full registry, first latch, overflow, 10000 concurrent records");
    return 0;
}
