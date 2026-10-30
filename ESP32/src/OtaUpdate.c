/* Author: R.E. van Eekelen.
 * ESP32 Ethernet OTA receiver. Never update while drive may run.
 * Transport: single TCP connection, authenticated with pre-shared HMAC-SHA256.
 * The Pi sends: WOTA1 <bytes> <sha256hex> <hmac_sha256hex>\n followed by image.
 * A fresh nonce is sent before each authenticated request.
 * The HMAC message is "WOTA1:<noncehex>:<bytes>:<sha256hex>".
 * OTA requires separate physical maintenance and motor isolation feedback.
 */
#include "OtaUpdate.h"
#include "OtaSafety.h"
#include "ApplicationTasks.h"
#include <unistd.h>
#include "AppConfig.h"
#include "TaskData.h"
#include "SafetyIO.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_app_desc.h"
#include "esp_system.h"
#include "esp_random.h"
#include "esp_log.h"
#include "mbedtls/md.h"
#include "mbedtls/sha256.h"
#include "lwip/sockets.h"
#include "driver/gpio.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdatomic.h>
#define OTA_PORT 5001
#define OTA_CHUNK 1024
static atomic_bool active = false;
bool OtaUpdate_Active(void) { return atomic_load(&active); }
#if OTA_MAINTENANCE_ENABLED
static bool maintenance_ok(void) {
#if OTA_MAINTENANCE_ENABLED
    const SafetySnapshot s=SafetyIO_Read();
    return OtaSafety_Authorized(true,
        gpio_get_level(OTA_MAINTENANCE_GPIO)==OTA_MAINTENANCE_ACTIVE_LEVEL,
        gpio_get_level(OTA_MOTOR_ISOLATED_GPIO)==OTA_MOTOR_ISOLATED_ACTIVE_LEVEL, &s);
#else
    return false;
#endif
}
static bool recv_all(int fd,void *dest,size_t amount) {
    uint8_t *p=dest; size_t got=0;
    while(got<amount) {
        int n=recv(fd,p+got,amount-got,0);
        if(n<=0) return false;
        got+=(size_t)n;
    }
    return true;
}
static bool line_read(int fd,char *s,size_t max) {
    size_t i=0;
    while(i+1<max) {
        char c;
        if(!recv_all(fd,&c,1)) return false;
        if(c=='\n'){s[i]=0;return true;}
        if(c<32||c>126) return false;
        s[i++]=c;
    }
    return false;
}
static void reply(int fd,const char *s) {(void)send(fd,s,strlen(s),0);}
static void handle_client(int fd) {
#if !OTA_MAINTENANCE_ENABLED
    reply(fd,"ERR OTA_DISABLED\n");
    return;
#else
    if(!maintenance_ok()){reply(fd,"ERR MAINTENANCE_REQUIRED\n");return;}
    char nonce[33];
    uint8_t random_bytes[16];
    for(unsigned i=0;i<sizeof(random_bytes);i+=4) {
        uint32_t r=esp_random();
        memcpy(random_bytes+i,&r,4);
    }
    for(unsigned i=0;i<16;i++) snprintf(nonce+2*i,3,"%02x",random_bytes[i]);
    char greeting[64];snprintf(greeting,sizeof(greeting),"NONCE %s\n",nonce);reply(fd,greeting);
    char header[220],hashhex[65],mac_hex[65],command[12],extra;
    unsigned long size=0;
    if(!line_read(fd,header,sizeof(header)) ||
       sscanf(header,"%11s %lu %64s %64s %c",command,&size,hashhex,mac_hex,&extra)!=4 ||
       strcmp(command,"WOTA1") || size==0 || size>0x300000UL ||
       strlen(hashhex)!=64 || strlen(mac_hex)!=64) {
        reply(fd,"ERR HEADER\n");return;
    }
    char message[175];
    int count=snprintf(message,sizeof(message),"WOTA1:%s:%lu:%s",nonce,size,hashhex);
    if(count<=0 || count>=(int)sizeof(message)){reply(fd,"ERR HEADER\n");return;}
    unsigned char calculated[32];
    const mbedtls_md_info_t *md=mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if(md==NULL || mbedtls_md_hmac(md,(const unsigned char *)OTA_SHARED_SECRET,
       strlen(OTA_SHARED_SECRET),(const unsigned char *)message,strlen(message),calculated)!=0) {
       reply(fd,"ERR AUTH\n");return;
    }
    char expected_mac[65];
    for(unsigned i=0;i<32;i++) snprintf(expected_mac+i*2,3,"%02x",calculated[i]);
    unsigned mismatch=0;
    for(unsigned i=0;i<64;i++) mismatch |= (unsigned char)(expected_mac[i]^mac_hex[i]);
    if(mismatch){reply(fd,"ERR AUTH\n");return;}
    if(!maintenance_ok()){reply(fd,"ERR MAINTENANCE_REQUIRED\n");return;}
    const esp_partition_t *target=esp_ota_get_next_update_partition(NULL);
    if(!target||size>target->size){reply(fd,"ERR PARTITION\n");return;}
    esp_ota_handle_t handle=0;
    if(esp_ota_begin(target,size,&handle)!=ESP_OK){reply(fd,"ERR BEGIN\n");return;}
    atomic_store(&active,true);
    reply(fd,"READY\n");
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    const bool hash_started=mbedtls_sha256_starts(&sha,0)==0;
    uint8_t buf[OTA_CHUNK];
    bool success=hash_started;
    for(unsigned long remaining=size;success && remaining>0;) {
        size_t n=remaining>sizeof(buf)?sizeof(buf):(size_t)remaining;
        if(!maintenance_ok() || !recv_all(fd,buf,n) || !maintenance_ok() || esp_ota_write(handle,buf,n)!=ESP_OK) {
            success=false;break;
        }
        if(mbedtls_sha256_update(&sha,buf,n)!=0) success=false;
        remaining-=n;
    }
    unsigned char digest[32];
    if(mbedtls_sha256_finish(&sha,digest)!=0) success=false;
    mbedtls_sha256_free(&sha);
    for(unsigned i=0;i<32;i++) {
        char pair[3];snprintf(pair,sizeof(pair),"%02x",digest[i]);
        if(pair[0]!=hashhex[i*2]||pair[1]!=hashhex[i*2+1]) success=false;
    }
    if(!success || !maintenance_ok()) {
        esp_ota_abort(handle); atomic_store(&active,false);
        reply(fd,"ERR VERIFY_OR_INTERLOCK\n");return;
    }
    if(esp_ota_end(handle)!=ESP_OK || !maintenance_ok() || esp_ota_set_boot_partition(target)!=ESP_OK) {
        atomic_store(&active,false);reply(fd,"ERR FINALIZE\n");return;
    }
    reply(fd,"OK REBOOT\n");
    shutdown(fd,SHUT_RDWR);
    vTaskDelay(pdMS_TO_TICKS(250));
    esp_restart();
#endif
}
#endif /* OTA_MAINTENANCE_ENABLED */
void OtaUpdateTask(void *argument) {
    (void)argument;
#if OTA_MAINTENANCE_ENABLED
    xEventGroupWaitBits(handle_ThreadEventGroup, EVT_PI_TASK_READY, pdFALSE, pdTRUE, portMAX_DELAY);
    gpio_config_t io={.pin_bit_mask=(1ULL<<OTA_MAINTENANCE_GPIO)|(1ULL<<OTA_MOTOR_ISOLATED_GPIO),
       .mode=GPIO_MODE_INPUT,.pull_up_en=GPIO_PULLUP_DISABLE,
       .pull_down_en=GPIO_PULLDOWN_DISABLE,.intr_type=GPIO_INTR_DISABLE};
    ESP_ERROR_CHECK(gpio_config(&io));
    int server=socket(AF_INET,SOCK_STREAM,IPPROTO_IP);
    ESP_ERROR_CHECK(server>=0?ESP_OK:ESP_FAIL);
    struct sockaddr_in a={.sin_family=AF_INET,.sin_port=htons(OTA_PORT),
       .sin_addr.s_addr=htonl(INADDR_ANY)};
    int reuse=1;setsockopt(server,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
    ESP_ERROR_CHECK(bind(server,(struct sockaddr*)&a,sizeof(a))==0?ESP_OK:ESP_FAIL);
    ESP_ERROR_CHECK(listen(server,1)==0?ESP_OK:ESP_FAIL);
    for(;;){
        int fd=accept(server,NULL,NULL);
        if(fd<0) {vTaskDelay(pdMS_TO_TICKS(100));continue;}
        struct timeval tv={.tv_sec=3,.tv_usec=0};
        setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));
        setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv));
        handle_client(fd);
        close(fd);
    }
#else
    vTaskDelete(NULL);
#endif
}

/* Confirm new OTA image only after a healthy control timer has run long enough.
 * A factory USB image remains the recovery baseline; failed OTA images roll back.
 */
void OtaMarkValidTask(void *argument) {
    (void)argument;
    const esp_partition_t *running=esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if(!running || esp_ota_get_state_partition(running,&state)!=ESP_OK ||
       state!=ESP_OTA_IMG_PENDING_VERIFY) {
        vTaskDelete(NULL);return;
    }
    vTaskDelay(pdMS_TO_TICKS(12000));
    Telemetry t={0};
    const bool good=xQueuePeek(telemetry,&t,0)==pdTRUE &&
        t.control_hz>185.0f && t.control_hz<215.0f &&
        t.overruns==0 && t.missed_deadlines==0;
    if(good)
        ESP_ERROR_CHECK(esp_ota_mark_app_valid_cancel_rollback());
    else
        (void)esp_ota_mark_app_invalid_rollback_and_reboot();
    vTaskDelete(NULL);
}
