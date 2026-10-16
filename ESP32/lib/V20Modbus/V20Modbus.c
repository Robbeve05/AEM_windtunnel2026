/* Siemens V20 Operating Instructions 12/2024, section 7.2.
 * PDU addresses are register number minus 40001, NOT printed 4xxxx numbers.
 * Fixed commissioning contract: P2000=50 Hz; 100% = 0x4000 = 50 Hz.
 */
#include "V20Modbus.h"
#include <math.h>
#include <string.h>
#define STW 99u
#define ZSW 109u
#define STOP 0x047eu
#define RUN  0x047fu
#define WATCHDOG_MS 500u

uint16_t V20_Crc(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xffffu;
    for (size_t i=0; i<length; ++i) {
        crc ^= data[i];
        for (unsigned j=0; j<8; ++j)
            crc = (uint16_t)((crc >> 1) ^ ((crc & 1u) ? 0xa001u : 0u));
    }
    return crc;
}
static void word(uint8_t *p, uint16_t value) { p[0]=(uint8_t)(value>>8); p[1]=(uint8_t)value; }
static uint16_t readword(const uint8_t *p) { return (uint16_t)((uint16_t)p[0]<<8 | p[1]); }
static bool exchange(V20 *d, uint8_t *tx, size_t n, uint8_t *rx, size_t expected)
{
    uint16_t crc=V20_Crc(tx,n);
    tx[n++]=(uint8_t)crc; tx[n++]=(uint8_t)(crc>>8);
    size_t received=d->exchange(d->context,tx,n,rx,16);
    if (received!=expected || received<5 || rx[0]!=d->slave || rx[1]!=tx[1]) return false;
    crc=V20_Crc(rx,received-2);
    return rx[received-2]==(uint8_t)crc && rx[received-1]==(uint8_t)(crc>>8);
}
static bool read_registers(V20 *d, uint16_t address, uint16_t count, uint16_t *values)
{
    uint8_t tx[8]={d->slave,3}, rx[16];
    word(tx+2,address); word(tx+4,count);
    if (!exchange(d,tx,6,rx,5u+2u*count) || rx[2]!=2u*count) return false;
    for (unsigned i=0; i<count; ++i) values[i]=readword(rx+3+2*i);
    return true;
}
static bool write_single(V20 *d, uint16_t address, uint16_t value)
{
    uint8_t tx[8]={d->slave,6},rx[16];
    word(tx+2,address); word(tx+4,value);
    return exchange(d,tx,6,rx,8) && memcmp(tx,rx,6)==0;
}
static bool command(V20 *d, bool run, float percent)
{
    uint8_t tx[13]={d->slave,16},rx[16];
    word(tx+2,STW); word(tx+4,2); tx[6]=4;
    word(tx+7,run ? RUN : STOP);
    word(tx+9,run ? (uint16_t)lroundf(percent*16384.0f/100.0f) : 0);
    return exchange(d,tx,11,rx,8) && memcmp(tx,rx,6)==0;
}
static bool status(V20 *d)
{
    uint16_t data[2];
    if (!read_registers(d,ZSW,2,data)) return false;
    d->status=data[0];
    const int32_t actual = data[1] < 32768u ? data[1] : (int32_t)data[1]-65536;
    d->actual_hz=(float)actual*50.0f/16384.0f;
    /* Ready to switch on, remote control requested; no fault or switch-on inhibit.
     * Do not require operation-enabled while stopped or during acceleration. */
    return (d->status & 0x0201u)==0x0201u && (d->status & 0x0048u)==0;
}
bool V20_Init(V20 *d, uint8_t slave, V20Exchange transport, void *context)
{
    if (!d || !transport || slave<1 || slave>247) return false;
    *d=(V20){.exchange=transport,.context=context,.slave=slave,.stop_required=true};
    uint16_t value;
    /* Stop first; never change frequency reference or acknowledge a drive fault. */
    if (!command(d,false,0) || !write_single(d,0,WATCHDOG_MS) ||
        !read_registers(d,0,1,&value) || value!=WATCHDOG_MS ||
        !read_registers(d,15,1,&value) || value!=5000u) return false;
    d->configured=true;
    /* Readiness can recover on later stopped cycles. Init success means configured. */
    return true;
}
bool V20_Write(V20 *d, bool run, float percent)
{
    if (!d || !d->configured) return false;
    bool valid=isfinite(percent) && percent>=0 && percent<=100;
    bool blocked=run && d->stop_required;
    if (!valid || blocked) {
        d->stop_required=true;
        (void)command(d,false,0);
        return false;
    }
    if (!command(d,run,percent) || !status(d)) {
        d->stop_required=true;
        /* Best effort OFF1, even after a failed or uncertain RUN acknowledgement. */
        (void)command(d,false,0);
        return false;
    }
    if (!run) d->stop_required=false;
    return true;
}
