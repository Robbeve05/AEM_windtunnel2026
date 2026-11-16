/* Author: R.E. van Eekelen. Real read-only command handling, no automatic verbose detail. */
#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "FaultRecorder.h"
#include "TaskData.h"
static char output[16384];
static unsigned capture_used, detail_calls;
static jmp_buf done;
static int capture_printf(const char *format, ...)
{
    va_list ap; va_start(ap,format);
    int n=vsnprintf(output+capture_used,sizeof(output)-capture_used,format,ap); va_end(ap);
    assert(n>=0 && (size_t)n<sizeof(output)-capture_used); capture_used+=(unsigned)n; return n;
}
static int capture_puts(const char *text) { return capture_printf("%s\n",text); }
static int capture_fputs(const char *text,FILE *stream) { (void)stream;return capture_printf("%s",text); }
#define printf capture_printf
#define puts capture_puts
#define fputs capture_fputs
#include "../src/FaultDiagnosticsTask.c"
#undef printf
#undef puts
#undef fputs
static Queue q;
QueueHandle_t telemetry=&q;
int xQueuePeek(QueueHandle_t queue,void *out,TickType_t wait)
{
    assert(queue==telemetry && wait==0);memcpy(out,q.data,sizeof(Telemetry));return pdTRUE;
}
size_t FaultSnapshot_Format(const FirstFaultSnapshot *fault,char *buffer,size_t size)
{
    assert(fault->valid && size>20);detail_calls++;memcpy(buffer,"FULL_CAPTURED_DETAIL\n",sizeof("FULL_CAPTURED_DETAIL\n"));return strlen(buffer);
}
int fcntl(int fd,int command,...) { assert(fd==0 && (command==F_GETFL || command==F_SETFL));return 0; }
int read(int fd,void *buffer,size_t size) { assert(fd==0 && size==1);(void)buffer;return 0; }
void vTaskDelay(TickType_t ticks) { assert(ticks==100);longjmp(done,1); }
int main(void)
{
    FaultRecorder_Reset();
    MachineInputs in={.now_ms=100,.manual_mode=true,.safety={.valid=true,.tick_ms=101,.snapshot_sequence=8}};
    FaultRecorder_Capture(FAULT_INTERLOCK,STATE_READY,&in,SAFETY_SNAPSHOT_STALE);
    Telemetry state={0};state.first_fault.valid=true;state.first_fault.code=FAULT_INTERLOCK;
    state.first_fault.triggering_inputs=in;memcpy(q.data,&state,sizeof(state));
    if(!setjmp(done))FaultDiagnosticsTask(NULL);
    assert(strstr(output,"F#1") && strstr(output,"age=INVALID(raw=4294967295)"));
    assert(!detail_calls && !strstr(output,"FULL_CAPTURED_DETAIL"));
    capture_used=0;output[0]=0;Command("FAULT?");Command("FAULT HISTORY");
    assert(!detail_calls && strstr(output,"episode=active"));
    Command("START");assert(FaultRecorder_Count()==1); /* Unknown commands have no effect. */
    Command("FAULT DETAIL");assert(detail_calls==1 && strstr(output,"FULL_CAPTURED_DETAIL"));
    state.first_fault.triggering_inputs.now_ms++;memcpy(q.data,&state,sizeof(state));
    Command("FAULT DETAIL");assert(detail_calls==1 && strstr(output,"full detail unavailable"));
    puts("PASS compact default, FAULT?/HISTORY/DETAIL, retained decision and no live-data substitution");
    return 0;
}
