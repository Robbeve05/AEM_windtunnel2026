#include "AppConfig.h"
#include "OtaSafety.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    assert(OTA_MAINTENANCE_ENABLED==0 && SAFETY_IO_COMMISSIONED==0);
    SafetySnapshot s={.valid=true,.commissioned=true,.estop_pressed=true,
        .contactor_available=true,.stop_ok=true};
    assert(OtaSafety_Authorized(true,true,true,&s));
    assert(!OtaSafety_Authorized(false,true,true,&s));
    assert(!OtaSafety_Authorized(true,false,true,&s));
    assert(!OtaSafety_Authorized(true,true,false,&s));
    s.contactor_available=false; assert(!OtaSafety_Authorized(true,true,true,&s));
    s.contactor_available=true;s.contactor_ok=true; assert(!OtaSafety_Authorized(true,true,true,&s));
    s.contactor_ok=false;s.relay_ok=true; assert(!OtaSafety_Authorized(true,true,true,&s));
    s.relay_ok=false;s.estop_pressed=false; assert(!OtaSafety_Authorized(true,true,true,&s));
    s.estop_pressed=true;s.start_pressed=true; assert(!OtaSafety_Authorized(true,true,true,&s));
    s.start_pressed=false;s.valid=false; assert(!OtaSafety_Authorized(true,true,true,&s));
    s.valid=true;s.commissioned=false; assert(!OtaSafety_Authorized(true,true,true,&s));
    puts("PASS OTA disabled by default; physical maintenance, isolation, contactor and safety authorization");
    return 0;
}
