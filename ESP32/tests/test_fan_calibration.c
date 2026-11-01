#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/FanCalibration.c"
static Table slots[2];static int selected=-1;static bool fail_write,fail_readback,safe=true;
int nvs_flash_init(void){return 0;}
int nvs_open(const char *n,int mode,nvs_handle_t *h){(void)n;(void)mode;*h=1;return 0;}
void nvs_close(nvs_handle_t h){(void)h;}
int nvs_get_blob(nvs_handle_t h,const char *k,void *p,size_t *n){(void)h;int i=!strcmp(k,"slot_b");if(fail_readback||!slots[i].count)return 1;memcpy(p,&slots[i],sizeof(Table));*n=sizeof(Table);return 0;}
int nvs_set_blob(nvs_handle_t h,const char *k,const void *p,size_t n){(void)h;assert(n==sizeof(Table));if(fail_write)return 1;memcpy(&slots[!strcmp(k,"slot_b")],p,n);return 0;}
int nvs_get_u8(nvs_handle_t h,const char *k,uint8_t *v){(void)h;(void)k;if(selected<0)return 1;*v=(uint8_t)selected;return 0;}
int nvs_set_u8(nvs_handle_t h,const char *k,uint8_t v){(void)h;(void)k;selected=v;return 0;}
int nvs_commit(nvs_handle_t h){(void)h;return 0;}
int nvs_erase_all(nvs_handle_t h){(void)h;selected=-1;memset(slots,0,sizeof(slots));return 0;}
void vTaskDelay(TickType_t t){(void)t;FanCalibration_ControlGate(safe);}
static bool command(const char *c){char reply[256];return FanCalibration_HandleCommand(c,true,true,reply,sizeof(reply));}
static void begin(Table *t){char cmd[100];snprintf(cmd,sizeof(cmd),"CAL_BEGIN 1 %u %lu",t->count,(unsigned long)t->crc);assert(command(cmd));}
static void upload(Table *t){begin(t);for(unsigned i=0;i<t->count;i++){char cmd[100];snprintf(cmd,sizeof(cmd),"CAL_POINT %u %lu %lu",i,(unsigned long)t->point[i].velocity,(unsigned long)t->point[i].frequency);assert(command(cmd));}}
int main(void){
 assert(!FanCalibration_Init());assert(FanCalibration_GetBaseFrequency(5,12)==12);
 Table t={.version=1,.count=2,.point={{10000,10000},{20000,25000}}};t.crc=checksum(&t);assert(t.crc==364007285u);
 /* Same canonical bytes and expected CRC are also tested by Python. */
 printf("CRC vector: %lu\n",(unsigned long)t.crc);
 begin(&t);assert(!command("CAL_COMMIT"));assert(!FanCalibration_IsValid());
 upload(&t);assert(!command("CAL_POINT 0 10000 10000"));assert(command("CAL_COMMIT"));
 assert(FanCalibration_IsValid());assert(FanCalibration_GetBaseFrequency(15,1)==17.5f);
 assert(FanCalibration_GetBaseFrequency(100,1)==25);assert(FanCalibration_GetBaseFrequency(5,4)==7);
 assert(FanCalibration_Init());assert(FanCalibration_GetBaseFrequency(15,1)==17.5f);
 t.point[1].frequency=30000;t.crc=checksum(&t);upload(&t);fail_write=true;assert(!command("CAL_COMMIT"));fail_write=false;
 assert(FanCalibration_Init());assert(FanCalibration_GetBaseFrequency(15,1)==17.5f);
 upload(&t);fail_readback=true;assert(!command("CAL_COMMIT"));fail_readback=false;assert(FanCalibration_Init());assert(FanCalibration_GetBaseFrequency(15,1)==17.5f);
 t.crc++;upload(&t);assert(!command("CAL_COMMIT"));assert(FanCalibration_IsValid());
 t.point[1].velocity=10000;t.crc=checksum(&t);upload(&t);assert(!command("CAL_COMMIT"));
 safe=false;assert(!command("CAL_RESET"));assert(FanCalibration_IsValid());safe=true;
 assert(command("CAL_RESET"));assert(!FanCalibration_IsValid());assert(!FanCalibration_Init());
 assert(!command("CAL_BEGIN 65537 2 0"));assert(!command("CAL_BEGIN 1 33 0"));
 puts("PASS CRC, incomplete upload, duplicate/order/checksum rejection, interpolation, fallback, NVS readback/reboot/failure/reset and control gate");
}
