///////////////////////////////////////////////////////////////////////////////
// File information

/* R.E. van Eekelen. Waveshare WS2812 status LED (non-safety). */
///////////////////////////////////////////////////////////////////////////////
// System includes
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"
#include <stdint.h>

///////////////////////////////////////////////////////////////////////////////
// Application includes
#include "AppConfig.h"
#include "StatusOutput.h"
#include "TaskData.h"

///////////////////////////////////////////////////////////////////////////////
// HeartbeatTask()

void HeartbeatTask(void *argument)
{
    (void)argument;
    rmt_channel_handle_t channel = NULL;
    rmt_encoder_handle_t encoder = NULL;
    const rmt_tx_channel_config_t cfg = {.gpio_num = BOARD_HEARTBEAT_LED_GPIO,
                                         .clk_src = RMT_CLK_SRC_DEFAULT,
                                         .resolution_hz = 10000000,
                                         .mem_block_symbols = 64,
                                         .trans_queue_depth = 2};
    const rmt_bytes_encoder_config_t bits = {
        .bit0 = {.level0 = 1, .duration0 = 4, .level1 = 0, .duration1 = 8},
        .bit1 = {.level0 = 1, .duration0 = 8, .level1 = 0, .duration1 = 4},
        .flags.msb_first = 1};
    esp_err_t rgb_error = rmt_new_tx_channel(&cfg, &channel);
    if (rgb_error == ESP_OK)
        rgb_error = rmt_new_bytes_encoder(&bits, &encoder);
    if (rgb_error == ESP_OK)
        rgb_error = rmt_enable(channel);
    const bool rgb_ready = rgb_error == ESP_OK;
    if (rgb_ready)
        ESP_LOGI("LED", "Onboard RGB status LED initialized");
    else
        ESP_LOGE("LED", "Onboard RGB status LED initialization failed: %s",
                 esp_err_to_name(rgb_error));
    const rmt_transmit_config_t tx = {.loop_count = 0};
    TickType_t wake = xTaskGetTickCount();
    uint32_t observed_control_cycles = 0, last_control_progress_ms = 0;
    bool have_control_progress = false;
    for (;;)
    {
        Telemetry state = {0};
        bool ok = xQueuePeek(telemetry, &state, 0) == pdTRUE;
        const uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        const StatusIndicator indication = StatusOutput_Get(&state, ok, now_ms);
        if (ok && state.control_cycle_count != observed_control_cycles)
        {
            observed_control_cycles = state.control_cycle_count;
            last_control_progress_ms = now_ms;
            have_control_progress = true;
        }
        const bool control_clock_active =
            ok && state.control_ready && state.control_clock_started && have_control_progress &&
            (uint32_t)(now_ms - last_control_progress_ms) <
                CONTROL_CLOCK_TIMEOUT_MS + STATUS_LED_UPDATE_PERIOD_MS;
        const bool control_clock_pulse =
            control_clock_active &&
            (now_ms % STATUS_LED_BLINK_PERIOD_MS) < 2 * STATUS_LED_UPDATE_PERIOD_MS;
        // Additive RGB rendering: INIT has both amber and red requests.
        uint8_t grb[3] = {indication.green || indication.amber ? 12 : 0,
                          indication.red     ? 25
                          : indication.amber ? 12
                                             : 0,
                          control_clock_pulse ? 18 : 0};
        if (rgb_ready)
        {
            (void)rmt_transmit(channel, encoder, grb, sizeof(grb), &tx);
            (void)rmt_tx_wait_all_done(channel, STATUS_RGB_TX_TIMEOUT_MS);
        }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(STATUS_LED_UPDATE_PERIOD_MS));
    }
}
