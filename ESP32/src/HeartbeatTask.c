/* R.E. van Eekelen. Waveshare WS2812 status LED (non-safety). */
#include "TaskData.h"
#include "AppConfig.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "esp_log.h"
#include <stdint.h>
void HeartbeatTask(void *argument) {
    (void)argument;
    rmt_channel_handle_t channel=NULL;
    rmt_encoder_handle_t encoder=NULL;
    const rmt_tx_channel_config_t cfg={.gpio_num=BOARD_HEARTBEAT_LED_GPIO,.clk_src=RMT_CLK_SRC_DEFAULT,.resolution_hz=10000000,.mem_block_symbols=64,.trans_queue_depth=2};
    const rmt_bytes_encoder_config_t bits={.bit0={.level0=1,.duration0=4,.level1=0,.duration1=8},.bit1={.level0=1,.duration0=8,.level1=0,.duration1=4},.flags.msb_first=1};
    if(rmt_new_tx_channel(&cfg,&channel)!=ESP_OK||rmt_new_bytes_encoder(&bits,&encoder)!=ESP_OK||rmt_enable(channel)!=ESP_OK){ESP_LOGE("LED","RMT init failed");vTaskDelete(NULL);return;}
    const rmt_transmit_config_t tx={.loop_count=0};
    TickType_t wake=xTaskGetTickCount();
    for(unsigned count=0;;count++){
        Telemetry state={0}; bool ok=xQueuePeek(telemetry,&state,0)==pdTRUE;
        unsigned phase=count%10;
        uint8_t grb[3]={0,0,0};
        if(!ok||state.state==STATE_INIT||state.state==STATE_WAIT){if(phase<5){grb[0]=8;grb[1]=12;}}
        else if(state.safety.estop_pressed||state.fault==FAULT_EMERGENCY_STOP){if(state.safety.estop_pressed||phase<5)grb[1]=25;}
        else if(state.state==STATE_FAULT){if(phase<3)grb[1]=25;}
        else if(state.state==STATE_READY)grb[0]=20;
        else if(state.state==STATE_RUNNING)grb[0]=phase<2?30:5;
        else if(state.state==STATE_STOP&&phase<5){grb[0]=8;grb[1]=12;}
        (void)rmt_transmit(channel,encoder,grb,sizeof(grb),&tx);
        (void)rmt_tx_wait_all_done(channel,pdMS_TO_TICKS(25));
        vTaskDelayUntil(&wake,pdMS_TO_TICKS(100));
    }
}
