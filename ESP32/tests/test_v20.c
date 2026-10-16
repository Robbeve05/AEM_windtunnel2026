#include "V20Modbus.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct { uint16_t stw, hsw, status, watchdog, reference, actual;
    unsigned commands; int damage; bool mismatch; } Fake;
static uint16_t w(const uint8_t *p) { return (uint16_t)((uint16_t)p[0]<<8 | p[1]); }
static void put(uint8_t *p,uint16_t v) { p[0]=(uint8_t)(v>>8); p[1]=(uint8_t)v; }
static size_t mock(void *context,const uint8_t *tx,size_t n,uint8_t *rx,size_t cap)
{
    Fake *f=context;
    assert(cap>=9 && n>=8);
    uint16_t crc=V20_Crc(tx,n-2);
    assert(tx[n-2]==(uint8_t)crc && tx[n-1]==(uint8_t)(crc>>8));
    assert(tx[0]==1);
    size_t size=8;
    memcpy(rx,tx,6);
    if (tx[1]==16) {
        assert(n==13 && w(tx+2)==99 && w(tx+4)==2 && tx[6]==4);
        f->stw=w(tx+7); f->hsw=w(tx+9); f->commands++;
        assert(f->stw==0x047e || f->stw==0x047f);
    } else if (tx[1]==6) {
        assert(w(tx+2)==0 && w(tx+4)==500);
        f->watchdog=w(tx+4);
    } else {
        assert(tx[1]==3);
        uint16_t address=w(tx+2),count=w(tx+4);
        rx[2]=(uint8_t)(2*count); size=5u+2u*count;
        if (address==0) put(rx+3,f->mismatch ? 0 : f->watchdog);
        else if (address==15) put(rx+3,f->reference);
        else { assert(address==109 && count==2); put(rx+3,f->status); put(rx+5,f->actual); }
    }
    int damage=f->damage; f->damage=0;
    if (damage==1) return 0; /* command applied, acknowledgement lost */
    if (damage==2) rx[0]=2;
    if (damage==3) { rx[1]=(uint8_t)(tx[1]|0x80); rx[2]=2; size=5; }
    if (damage==4) rx[3]^=1; /* wrong write address, valid CRC */
    if (damage==6) rx[1]=4;
    if (damage==7) rx[5]^=1; /* wrong write count */
    crc=V20_Crc(rx,size-2); rx[size-2]=(uint8_t)crc; rx[size-1]=(uint8_t)(crc>>8);
    if (damage==5) rx[size-1]^=1;
    if (damage==8) size--;
    return size;
}
int main(void)
{
    assert(V20_Crc((const uint8_t *)"123456789",9)==0x4b37);
    Fake f={.reference=5000,.status=0x0201,.actual=8192}; V20 d;
    assert(!V20_Init(&d,0,mock,&f));
    assert(V20_Init(&d,1,mock,&f));
    assert(f.stw==0x047e && f.hsw==0 && f.watchdog==500);
    assert(!V20_Write(&d,true,50)); /* require confirmed stopped cycle first */
    assert(V20_Write(&d,false,0));
    assert(fabsf(d.actual_hz-25)<.001f);
    assert(V20_Write(&d,true,50) && f.stw==0x047f && f.hsw==8192);
    assert(V20_Write(&d,true,100) && f.hsw==16384);
    assert(V20_Write(&d,true,0) && f.stw==0x047f && f.hsw==0);
    assert(V20_Write(&d,false,100) && f.stw==0x047e && f.hsw==0);
    for (int damage=1; damage<=8; ++damage) {
        f.damage=damage;
        assert(!V20_Write(&d,true,50));
        assert(f.stw==0x047e && f.hsw==0 && d.stop_required);
        assert(!V20_Write(&d,true,50));
        assert(V20_Write(&d,false,0));
    }
    const uint16_t bad[]={0x0209,0x0241,0x0001,0x0200};
    for (unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
        f.status=bad[i]; assert(!V20_Write(&d,true,50) && f.stw==0x047e);
        f.status=0x0201; assert(V20_Write(&d,false,0));
    }
    assert(!V20_Write(&d,true,NAN) && f.stw==0x047e);
    assert(!V20_Write(&d,true,101));
    assert(!V20_Write(&d,true,-1));
    f.actual=0xe000; assert(V20_Write(&d,false,0) && d.actual_hz==-25);
    f.reference=6000; assert(!V20_Init(&d,1,mock,&f) && !d.configured);
    f.reference=5000; f.mismatch=true; assert(!V20_Init(&d,1,mock,&f));
    puts("PASS: V20 scaling, CRC, malformed/exception replies, timeout, stop latch, drive faults, commissioning checks");
    return 0;
}
