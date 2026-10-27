#include "TaskData.h"
#include "AppConfig.h"
#include "PiProtocol.h"
#include "PressureSensors.h"
#include "SafetyIO.h"
#include "lwip/sockets.h"
#include <errno.h>
#include <unistd.h>
#include "freertos/task.h"
#include "esp_err.h"
#include <stdio.h>
#include <string.h>

void Ethernet_Init(void);
static int client=-1;
static void disconnect_client(void) {
    if (client>=0) { shutdown(client,SHUT_RDWR); close(client); client=-1; }
    PiHeartbeat heartbeat={0}; xQueueOverwrite(pi_heartbeat,&heartbeat);
    xEventGroupSetBits(events,STOP_REQUEST);
    xQueueReset(commands);
}
static void reply(const char *text) {
    size_t remaining=strlen(text);
    while (remaining && client>=0) {
        int n=send(client,text,remaining,0);
        if (n<=0) { disconnect_client(); return; }
        remaining-=n; text+=n;
    }
}
static void process(const char *line)
{
#if SIMULATION_MODE
    char name[16],extra; int active;
    if(sscanf(line,"SIM %15s %d %c",name,&active,&extra)==2&&(active==0||active==1)) {
        reply(SafetyIO_Simulate(name,active!=0)?"OK SIM\n":"ERR SIM\n"); return;
    }
#endif
    PiCommand c;
    if (!PiProtocol_Parse(line, &c)) { reply("ERR SYNTAX\n"); return; }
    if(c.type==CMD_PING) {
        PiHeartbeat heartbeat={.connected=true,.tick=xTaskGetTickCount()};
        xQueueOverwrite(pi_heartbeat,&heartbeat); reply("OK PING\n"); return;
    }
#if !ALLOW_REMOTE_START
    if(c.type==CMD_START) { reply("ERR PHYSICAL_START_REQUIRED\n"); return; }
#endif
    if ((c.type==CMD_SETPOINT && (c.values[0]<0 || c.values[0]>MAX_SETPOINT)) ||
        (c.type==CMD_PID && (c.values[0]<0 || c.values[0]>1000 || c.values[1]<0 ||
          c.values[1]>1000 || c.values[2]<0 || c.values[2]>1000))) {
        reply("ERR RANGE\n"); return;
    }
    if (c.type==CMD_FILTER) {
        if (c.values[0]<0.1f || c.values[0]>10.0f) { reply("ERR RANGE\n"); return; }
        SensorLib_SetFilter(c.values[0]); reply("OK FILTER\n"); return;
    }
    if (c.type==CMD_STOP) {
        xEventGroupSetBits(events, STOP_REQUEST);
        reply("OK STOP_REQUESTED\n");
    } else if (xQueueSend(commands, &c, 0)==pdTRUE) reply("OK QUEUED\n");
    else reply("ERR BUSY\n");
}
void PiCommunicationTask(void *argument)
{
    (void)argument;
    Ethernet_Init();
    int server=socket(AF_INET,SOCK_STREAM,IPPROTO_IP);
    ESP_ERROR_CHECK(server>=0 ? ESP_OK : ESP_FAIL);
    int reuse=1;
    setsockopt(server,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
    struct sockaddr_in address={.sin_family=AF_INET,.sin_port=htons(PI_TCP_PORT),
        .sin_addr.s_addr=htonl(INADDR_ANY)};
    ESP_ERROR_CHECK(bind(server,(struct sockaddr *)&address,sizeof(address))==0 ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(listen(server,1)==0 ? ESP_OK : ESP_FAIL);
    char line[96]; size_t used=0; bool overflow=false;
    TickType_t sent=xTaskGetTickCount();
    for (;;) {
        if (client<0) {
            client=accept(server,NULL,NULL);
            if (client<0) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
            struct timeval timeout={.tv_usec=10000};
            setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
            timeout.tv_usec=100000;
            setsockopt(client,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
            used=0; overflow=false;
            xQueueReset(commands); xEventGroupSetBits(events,STOP_REQUEST);
        }
        uint8_t data[64];
        int count=recv(client,data,sizeof(data),0);
        if (count==0 || (count<0 && errno!=EAGAIN && errno!=EWOULDBLOCK)) {
            disconnect_client(); continue;
        }
        for (int i=0; i<count && client>=0; ++i) {
            char c=(char)data[i];
            if (c=='\n') {
                if (overflow) reply("ERR LINE\n");
                else { line[used]='\0'; process(line); }
                used=0; overflow=false;
            } else if (c=='\r') { /* CRLF is accepted. */ }
            else if ((unsigned char)c < 32 || (unsigned char)c > 126) overflow=true;
            else if (!overflow && used<sizeof(line)-1) line[used++]=c;
            else overflow=true;
        }
        TickType_t now=xTaskGetTickCount();
        if ((TickType_t)(now-sent)>=pdMS_TO_TICKS(TELEMETRY_PERIOD_MS)) {
            Telemetry t;
            if (xQueuePeek(telemetry, &t, 0)==pdTRUE) {
                Sample sample={0}; MotorStatus motor={0};
                xQueuePeek(sensors,&sample,0); xQueuePeek(motor_status,&motor,0);
                char buffer[2304];
                int length=snprintf(buffer,sizeof(buffer),
                    "STATUS running=%d sensor_ok=%d motor_ok=%d sp=%.3f pv=%.3f out=%.3f reason=%s sim=%d sp_ramp=%.3f"
                    " pressure_pa=%.3f temperature_c=%.3f actual_hz=%.3f desired_hz=%.3f drive_status=%u drive_fault=%d"
                    " control_hz=%.2f execution_us=%lu max_jitter_us=%lu missed_deadlines=%lu overruns=%lu"
                    " sensor_errors=%lu modbus_errors=%lu modbus_timeouts=%lu ethernet=1 stack_control=%lu stack_ethernet=%u"
                    " taps_valid=%u filter_hz=%.2f sample_seq=%lu"
                    " state=%s fault=%s last_fault=%s estop=%d relay=%d reset_required=%d stop=%d inputs_valid=%d pi=%d commissioned=%d"
                    " estop_pressed=%d safety_relay_ok=%d stop_button_ok=%d pi_connected=%d sensor_age_ms=%lu"
                    " raw_di=%lu start=%d reset=%d vfd_fault_available=%d contactor_available=%d contactor=%d"
                    " control_period_us=%u service_us=%lu service_max_us=%lu rtt_median_us=%lu rtt_p95_us=%lu rtt_max_us=%lu request_id=%lu modbus_hz=%.2f transitions=%lu remote_start=%d crc_errors=%lu exceptions=%lu exception_code=%lu late_replies=%lu",
                    t.running,(sample.valid && xTaskGetTickCount()-sample.tick<pdMS_TO_TICKS(SENSOR_TIMEOUT_MS)),t.motor_ok,t.setpoint,sample.value,t.output,t.reason,SIMULATION_MODE,t.control_setpoint,
                    sample.pressure_pa,sample.temperature_c,motor.actual_hz,t.output*0.5f,motor.status,!!(motor.status&8),
                    t.control_hz,(unsigned long)t.execution_us,(unsigned long)t.max_jitter_us,
                    (unsigned long)t.missed_deadlines,(unsigned long)t.overruns,(unsigned long)sample.errors,
                    (unsigned long)motor.errors,(unsigned long)motor.timeouts,(unsigned long)t.stack_free,
                    (unsigned)uxTaskGetStackHighWaterMark(NULL),sample.taps_valid,SensorLib_FilterHz(),(unsigned long)sample.sequence,
                    StateMachine_Name(t.state),FaultReason_Name(t.fault),FaultReason_Name(t.last_fault),
                    t.safety.estop_pressed,t.safety.relay_ok,t.reset_required,!t.safety.stop_ok,t.safety.valid,t.pi_ok,t.safety.commissioned,
                    t.safety.estop_pressed,t.safety.relay_ok,t.safety.stop_ok,t.pi_ok,(unsigned long)((xTaskGetTickCount()-sample.tick)*portTICK_PERIOD_MS),
                    (unsigned long)t.safety.raw_levels,t.safety.start_pressed,t.safety.reset_pressed,t.safety.vfd_fault_available,
                    t.safety.contactor_available,t.safety.contactor_ok,CONTROL_PERIOD_US,
                    (unsigned long)t.service_us,(unsigned long)t.service_max_us,(unsigned long)t.rtt_median_us,
                    (unsigned long)t.rtt_p95_us,(unsigned long)t.rtt_max_us,(unsigned long)t.request_id,t.modbus_hz,
                    (unsigned long)t.transitions,ALLOW_REMOTE_START,(unsigned long)t.crc_errors,(unsigned long)t.exceptions,(unsigned long)t.exception_code,(unsigned long)t.late_replies);
                for (int tap=0;tap<16 && length>0 && length<(int)sizeof(buffer)-32;tap++)
                    length+=snprintf(buffer+length,sizeof(buffer)-length," p%d=%.3f",tap,
                        (sample.taps_valid&(1u<<tap)) ? sample.taps[tap] : 0.0f);
                if (length>0 && length<(int)sizeof(buffer)-2) { buffer[length++]='\n'; buffer[length]=0; }
                reply(buffer);
            }
            sent=now;
        }
    }
}
