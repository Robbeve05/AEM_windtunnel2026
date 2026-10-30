/* R.E. van Eekelen. Waveshare WS2812 status LED (non-safety). */
#include "TaskData.h"
#include "AppConfig.h"
#include "StatusOutput.h"
#include "DigitalOutputs.h"
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
    bool rgb_ready=rmt_new_tx_channel(&cfg,&channel)==ESP_OK && rmt_new_bytes_encoder(&bits,&encoder)==ESP_OK && rmt_enable(channel)==ESP_OK;
    if(!rgb_ready) ESP_LOGE("LED","RMT init failed; external status requests continue");
    const rmt_transmit_config_t tx={.loop_count=0};
    TickType_t wake=xTaskGetTickCount();
    for(;;){
        Telemetry state={0}; bool ok=xQueuePeek(telemetry,&state,0)==pdTRUE;
        const StatusIndicator indication=StatusOutput_Get(&state,ok,
            (uint32_t)(xTaskGetTickCount()*portTICK_PERIOD_MS));
        (void)DigitalOutputs_SetStatusLeds(indication.green,indication.amber,indication.red);
        // Additive RGB rendering: INIT has both amber and red requests.
        uint8_t grb[3]={indication.green||indication.amber ? 12:0,
                        indication.red ? 25:indication.amber ? 12:0,0};
        if(rgb_ready) {
            (void)rmt_transmit(channel,encoder,grb,sizeof(grb),&tx);
            (void)rmt_tx_wait_all_done(channel,STATUS_RGB_TX_TIMEOUT_MS);
        }
        vTaskDelayUntil(&wake,pdMS_TO_TICKS(STATUS_LED_UPDATE_PERIOD_MS));
    }
}
