/* Author: R.E. van Eekelen. Exercise the real non-safety LED task and async buffer ownership. */
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "TaskData.h"
const char *esp_err_to_name(int error);
#include "../src/HeartbeatTask.c"

QueueHandle_t telemetry;
static Queue queue;
static jmp_buf finished;
static unsigned cycles, peeks, transmits, waits, creates, integrity_checks;
static int fail_init, fail_transmit, fail_wait;
static const uint8_t *pending;
static uint8_t submitted[3];

const char *esp_err_to_name(int error) { (void)error; return "injected RMT error"; }
TickType_t xTaskGetTickCount(void) { return 250u + cycles * 100u; }
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t task) { assert(!task); return 1500; }
bool heap_caps_check_integrity_all(bool print_errors) {
    assert(print_errors); integrity_checks++; return true;
}
int xQueuePeek(QueueHandle_t q, void *data, TickType_t wait) {
    assert(q == telemetry && q->size == sizeof(Telemetry) && !wait);
    peeks++;
    if (!q->count) return pdFALSE;
    memcpy(data, q->data, q->size);
    return pdTRUE;
}
static void next_cycle(void) {
    if (pending) assert(memcmp(pending, submitted, sizeof(submitted)) == 0);
    /* The queue becomes available after the first iteration. */
    Telemetry state = {.state = STATE_RUNNING, .control_cycle_count = cycles + 1u};
    memcpy(queue.data, &state, sizeof(state)); queue.count = 1;
    if (++cycles == 3) longjmp(finished, 1);
}
void vTaskDelayUntil(TickType_t *wake, TickType_t ticks) {
    assert(wake && ticks == STATUS_LED_UPDATE_PERIOD_MS); next_cycle();
}
void vTaskDelay(TickType_t ticks) { assert(ticks == STATUS_LED_UPDATE_PERIOD_MS); next_cycle(); }
esp_err_t rmt_new_tx_channel(const rmt_tx_channel_config_t *cfg, rmt_channel_handle_t *out) {
    creates++; assert(cfg->gpio_num == BOARD_HEARTBEAT_LED_GPIO && cfg->trans_queue_depth == 2);
    if (fail_init) return ESP_FAIL;
    *out = (void *)1; return ESP_OK;
}
esp_err_t rmt_new_bytes_encoder(const rmt_bytes_encoder_config_t *cfg, rmt_encoder_handle_t *out) {
    assert(cfg->bit0.duration0 == 4 && cfg->bit1.duration0 == 8 && cfg->flags.msb_first);
    *out = (void *)2; return ESP_OK;
}
esp_err_t rmt_enable(rmt_channel_handle_t channel) { assert(channel == (void *)1); return ESP_OK; }
esp_err_t rmt_transmit(rmt_channel_handle_t channel, rmt_encoder_handle_t encoder,
                       const void *payload, size_t size, const rmt_transmit_config_t *cfg) {
    assert(channel == (void *)1 && encoder == (void *)2 && size == 3 && cfg->loop_count == 0);
    assert(!pending);
    transmits++;
    const uint8_t *rgb = payload;
    if (transmits == 1) assert(rgb[0] == 0 && rgb[1] == 25 && rgb[2] == 0); /* unavailable */
    else assert(rgb[0] == 12 && rgb[1] == 0 && rgb[2] == 0); /* running */
    if (fail_transmit) return ESP_FAIL;
    pending = payload; memcpy(submitted, payload, 3); return ESP_OK;
}
esp_err_t rmt_tx_wait_all_done(rmt_channel_handle_t channel, int timeout) {
    assert(channel == (void *)1 && timeout == STATUS_RGB_TX_TIMEOUT_MS && pending);
    waits++;
    if (fail_wait) return ESP_ERR_TIMEOUT; /* ISR may still be using payload */
    pending = NULL; return ESP_OK;
}
static void run_case(int init, int transmit, int wait) {
    cycles = peeks = creates = transmits = waits = integrity_checks = 0;
    fail_init = init; fail_transmit = transmit; fail_wait = wait; pending = NULL;
    queue = (Queue){.size = sizeof(Telemetry)}; telemetry = &queue;
    if (!setjmp(finished)) HeartbeatTask(NULL);
    assert(cycles == 3);
#if HEARTBEAT_DIAGNOSTIC_MODE == 1
    assert(!creates && !peeks && !transmits);
#elif HEARTBEAT_DIAGNOSTIC_MODE == 2
    assert(!creates && peeks == 3 && !transmits);
#else
    assert(creates == 1 && peeks == 3);
    assert(transmits == (init ? 0u : (transmit || wait) ? 1u : 3u));
    assert(waits == (init || transmit ? 0u : wait ? 1u : 3u));
#endif
#if HEARTBEAT_DIAGNOSTICS && HEARTBEAT_DIAGNOSTIC_MODE != 1
    assert(integrity_checks == 3);
#endif
}
int main(void) {
    run_case(0, 0, 0); run_case(1, 0, 0); run_case(0, 1, 0); run_case(0, 0, 1);
    puts("PASS real Heartbeat: unavailable/running telemetry, RMT failures, pending payload preserved, isolation modes");
    return 0;
}
