#include "AppConfig.h"
#include "WindtunnelHAL.h"
#if !SIMULATION_MODE
#include "V20Modbus.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static V20 drive;
static bool uart_ready;
/* UART driver controls RTS/DE until the last stop bit has left the peripheral.
 * Connect DE and /RE together to GPIO27; use a 3.3 V RS485 transceiver. */
static size_t transfer(void *context, const uint8_t *tx, size_t length,
                       uint8_t *rx, size_t capacity)
{
    (void)context;
    /* >3.5 characters at 19200 8E1. Also discard late data from an earlier request. */
    vTaskDelay(pdMS_TO_TICKS(3)+1);
    if (uart_flush_input(MODBUS_UART_PORT)!=ESP_OK) return 0;
    if (uart_write_bytes(MODBUS_UART_PORT,tx,length)!=(int)length ||
        uart_wait_tx_done(MODBUS_UART_PORT,pdMS_TO_TICKS(MODBUS_RESPONSE_TIMEOUT_MS))!=ESP_OK)
        return 0;
    const int64_t deadline=esp_timer_get_time()+1000LL*MODBUS_RESPONSE_TIMEOUT_MS;
    size_t used=0, expected=0;
    while (used<capacity && esp_timer_get_time()<deadline) {
        int n=uart_read_bytes(MODBUS_UART_PORT,rx+used,1,pdMS_TO_TICKS(1)+1);
        if (n<0) break;
        if (n==0) continue;
        used++;
        if (used==2) {
            if (rx[1]&0x80u) expected=5;
            else if (rx[1]==6 || rx[1]==16) expected=8;
            else if (rx[1]!=3) break;
        }
        if (used==3 && rx[1]==3) expected=(size_t)rx[2]+5;
        if (expected>capacity) break;
        if (expected && used==expected) return used;
    }
    /* Quarantine late responses after timeout; no immediate identical retry. */
    vTaskDelay(pdMS_TO_TICKS(MODBUS_RESPONSE_TIMEOUT_MS)+1);
    uart_flush_input(MODBUS_UART_PORT);
    return 0;
}
bool MotorLib_Init(void)
{
    if (!uart_ready) {
        const uart_config_t config={.baud_rate=MODBUS_BAUD,.data_bits=UART_DATA_8_BITS,
            .parity=UART_PARITY_EVEN,.stop_bits=UART_STOP_BITS_1,
            .flow_ctrl=UART_HW_FLOWCTRL_DISABLE,.source_clk=UART_SCLK_APB};
        if (uart_param_config(MODBUS_UART_PORT,&config)!=ESP_OK ||
            uart_set_pin(MODBUS_UART_PORT,MODBUS_TX_PIN,MODBUS_RX_PIN,
                         MODBUS_DE_PIN,UART_PIN_NO_CHANGE)!=ESP_OK ||
            uart_driver_install(MODBUS_UART_PORT,256,0,0,NULL,0)!=ESP_OK) return false;
        if (uart_set_mode(MODBUS_UART_PORT,UART_MODE_RS485_HALF_DUPLEX)!=ESP_OK) {
            uart_driver_delete(MODBUS_UART_PORT);
            return false;
        }
        uart_ready=true;
    }
    return V20_Init(&drive,MODBUS_SLAVE_ADDRESS,transfer,NULL);
}
bool MotorLib_Write(bool run, float percentage) { return V20_Write(&drive,run,percentage); }
#endif
