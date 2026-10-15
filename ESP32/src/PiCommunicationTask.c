#include "TaskData.h"
#include "AppConfig.h"
#include "PiProtocol.h"
#include "driver/uart.h"
#include "freertos/task.h"
#include "esp_err.h"
#include <stdio.h>
#include <string.h>

static void reply(const char *text) { uart_write_bytes(PI_UART_PORT, text, strlen(text)); }
static void process(const char *line)
{
    PiCommand c;
    if (!PiProtocol_Parse(line, &c)) { reply("ERR SYNTAX\n"); return; }
    if ((c.type==CMD_SETPOINT && (c.values[0]<0 || c.values[0]>MAX_SETPOINT)) ||
        (c.type==CMD_PID && (c.values[0]<0 || c.values[0]>1000 || c.values[1]<0 ||
          c.values[1]>1000 || c.values[2]<0 || c.values[2]>1000))) {
        reply("ERR RANGE\n"); return;
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
    const uart_config_t config = {.baud_rate=PI_UART_BAUD, .data_bits=UART_DATA_8_BITS,
        .parity=UART_PARITY_DISABLE, .stop_bits=UART_STOP_BITS_1, .flow_ctrl=UART_HW_FLOWCTRL_DISABLE,
        .source_clk=UART_SCLK_APB};
    ESP_ERROR_CHECK(uart_param_config(PI_UART_PORT, &config));
    ESP_ERROR_CHECK(uart_set_pin(PI_UART_PORT, PI_UART_TX_PIN, PI_UART_RX_PIN,
        UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(PI_UART_PORT, 1024, 0, 0, NULL, 0));
    char line[96]; size_t used=0; bool overflow=false;
    TickType_t sent=xTaskGetTickCount();
    for (;;) {
        uint8_t data[64];
        int count=uart_read_bytes(PI_UART_PORT, data, sizeof(data), pdMS_TO_TICKS(10));
        for (int i=0; i<count; ++i) {
            char c=(char)data[i];
            if (c=='\n') {
                if (overflow) reply("ERR LINE\n");
                else { line[used]='\0'; process(line); }
                used=0; overflow=false;
            } else if (c=='\r') { /* CRLF is toegestaan. */ }
            else if ((unsigned char)c < 32 || (unsigned char)c > 126) overflow=true;
            else if (!overflow && used<sizeof(line)-1) line[used++]=c;
            else overflow=true;
        }
        TickType_t now=xTaskGetTickCount();
        if ((TickType_t)(now-sent)>=pdMS_TO_TICKS(TELEMETRY_PERIOD_MS)) {
            Telemetry t;
            if (xQueuePeek(telemetry, &t, 0)==pdTRUE) {
                char buffer[256];
                snprintf(buffer, sizeof(buffer),
                    "STATUS running=%d sensor_ok=%d motor_ok=%d sp=%.3f pv=%.3f out=%.3f reason=%s sim=%d\n",
                    t.running, t.sensor_ok, t.motor_ok, t.setpoint, t.measurement, t.output, t.reason, SIMULATION_MODE);
                reply(buffer);
            }
            sent=now;
        }
    }
}
