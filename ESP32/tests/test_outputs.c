/* Author: R.E. van Eekelen. Real host threads exercise production output locks. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include "AppConfig.h"
#include "DigitalOutputs.h"
#include "I2CBus.h"
#include "freertos/semphr.h"
static CRITICAL_SECTION critical;
static __declspec(thread) int critical_depth;
static LONG entered, exited, writes;
static bool no_mutex, inhibited;
static LONG fail_write;
static uint8_t registers[128],images[128];
static HANDLE write_started, release_write;
static LONG pause_write;
void Host_EnterCritical(portMUX_TYPE *lock) {
    (void)lock; EnterCriticalSection(&critical); critical_depth++; InterlockedIncrement(&entered);
}
void Host_ExitCritical(portMUX_TYPE *lock) {
    (void)lock; assert(critical_depth==1); critical_depth--; InterlockedIncrement(&exited); LeaveCriticalSection(&critical);
}
SemaphoreHandle_t xSemaphoreCreateMutex(void) {return no_mutex ? NULL : CreateMutex(NULL,FALSE,NULL);}
BaseType_t xSemaphoreTake(SemaphoreHandle_t mutex,TickType_t wait) {
    return WaitForSingleObject(mutex,wait)==WAIT_OBJECT_0 ? pdTRUE:pdFALSE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t mutex) {return ReleaseMutex(mutex) ? pdTRUE:pdFALSE;}
bool SafetyIO_Inhibited(void) {return inhibited;}
bool I2CBus_Write(uint8_t address,const uint8_t *data,size_t size) {
    assert(critical_depth==0 && address==BOARD_DO_I2C_ADDRESS && size==2);
    LONG n=InterlockedIncrement(&writes);
    assert(n<=128); registers[n-1]=data[0];images[n-1]=data[1];
    if(InterlockedCompareExchange(&pause_write,0,1)==1) {
        SetEvent(write_started); assert(WaitForSingleObject(release_write,5000)==WAIT_OBJECT_0);
    }
    return n!=fail_write;
}
static bool vfd_on(uint8_t image) {return !!(image&(1u<<VFD_ENABLE_DO_BIT))==(VFD_ENABLE_ACTIVE_LEVEL!=0);}
static DWORD WINAPI enable_thread(LPVOID unused) {(void)unused;assert(DigitalOutputs_Set(true,(StatusIndicator){0}));return 0;}
static DWORD WINAPI disable_thread(LPVOID unused) {(void)unused;assert(DigitalOutputs_Set(false,(StatusIndicator){0}));return 0;}
int main(void) {
    InitializeCriticalSection(&critical);
    const unsigned bits[]={VFD_ENABLE_DO_BIT,GREEN_INDICATOR_DO_BIT,RED_INDICATOR_DO_BIT,
        YELLOW_INDICATOR_DO_BIT,
        SPARE_DO5_BIT,SPARE_DO6_BIT,SPARE_DO7_BIT,SPARE_DO8_BIT};
    for(unsigned i=0;i<8;i++) {assert(bits[i]==i);for(unsigned j=0;j<i;j++) assert(bits[i]!=bits[j]);}
    assert(VFD_ENABLE_DO_BIT==0 && GREEN_INDICATOR_DO_BIT==1 &&
        RED_INDICATOR_DO_BIT==2 && YELLOW_INDICATOR_DO_BIT==3);
    DigitalOutputStatus s;
    no_mutex=true;assert(!DigitalOutputs_Init());no_mutex=false;
    assert(DigitalOutputs_Init());
    assert(writes==2 && registers[0]==1 && registers[1]==3 && images[1]==0);
    assert(!vfd_on(images[0]) && images[0]==0xffu);
    DigitalOutputs_GetStatus(&s);assert(s.initialized && s.confirmed_valid && !s.vfd_requested &&
        !s.vfd_acknowledged);
    assert(DigitalOutputs_Set(true,(StatusIndicator){0}));DigitalOutputs_GetStatus(&s);
    assert(s.vfd_requested==(SAFETY_IO_COMMISSIONED!=0));
    assert(s.vfd_acknowledged==(SAFETY_IO_COMMISSIONED!=0) && s.confirmed_valid);
    assert(vfd_on(s.confirmed_image)==(SAFETY_IO_COMMISSIONED!=0));
    assert((s.confirmed_image&0xfeu)==0xfeu);
    assert(DigitalOutputs_Set(false,(StatusIndicator){0}));DigitalOutputs_GetStatus(&s);
    assert(!s.vfd_requested && !s.vfd_acknowledged && !vfd_on(s.confirmed_image));
    assert(s.confirmed_image==0xffu);
    assert(DigitalOutputs_Set(false,(StatusIndicator){.red=true,.amber=true}));DigitalOutputs_GetStatus(&s);
    assert(s.confirmed_image==0xf3u);
    assert(DigitalOutputs_Set(false,(StatusIndicator){.green=true}));DigitalOutputs_GetStatus(&s);
    assert(s.confirmed_image==0xfdu);
    // A concurrent disable waits for the current I2C image write to finish.
    write_started=CreateEvent(NULL,TRUE,FALSE,NULL);release_write=CreateEvent(NULL,TRUE,FALSE,NULL);
    InterlockedExchange(&pause_write,1);
    HANDLE enable=CreateThread(NULL,0,enable_thread,NULL,0,NULL);
    assert(WaitForSingleObject(write_started,5000)==WAIT_OBJECT_0);
    HANDLE disable=CreateThread(NULL,0,disable_thread,NULL,0,NULL);
    SetEvent(release_write);
    assert(WaitForSingleObject(enable,5000)==WAIT_OBJECT_0 && WaitForSingleObject(disable,5000)==WAIT_OBJECT_0);
    DigitalOutputs_GetStatus(&s);assert(!s.vfd_requested && !vfd_on(s.confirmed_image));
    assert(s.confirmed_image==0xffu);
    CloseHandle(enable);CloseHandle(disable);CloseHandle(write_started);CloseHandle(release_write);
    inhibited=true;assert(DigitalOutputs_Set(true,(StatusIndicator){0}));DigitalOutputs_GetStatus(&s);assert(!s.vfd_requested && !vfd_on(s.confirmed_image));
    assert(s.confirmed_image==0xffu);
    inhibited=false;fail_write=writes+1;assert(!DigitalOutputs_Set(true,(StatusIndicator){0}));
    DigitalOutputs_GetStatus(&s);assert(!s.confirmed_valid && s.write_errors==1);
    fail_write=0;assert(DigitalOutputs_Set(false,(StatusIndicator){0}));DigitalOutputs_GetStatus(&s);assert(s.confirmed_valid && !vfd_on(s.confirmed_image));
    // Failing direction setup never publishes initialized/confirmed outputs.
    fail_write=writes+2;assert(!DigitalOutputs_Init());DigitalOutputs_GetStatus(&s);
    assert(!s.initialized && !s.confirmed_valid && s.write_errors==1 && !DigitalOutputs_Set(true,(StatusIndicator){0}));
    assert(entered==exited && entered>20);
    puts("PASS pin uniqueness, inactive output startup, DO1-only authorization, unassigned DO2-DO4, serialized requests, initialization/write failures");
    return 0;
}
