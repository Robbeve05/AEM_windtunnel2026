#pragma once
/* Author: R.E. van Eekelen. Physical assignments only. */
// DIGITAL INPUTS - existing optically isolated DI1..DI8
#define SAFETY_RELAY_OK_GPIO 4
#define ESTOP_PRESSED_GPIO 5
#define STOP_BUTTON_OK_GPIO 6
#define START_BUTTON_GPIO 7
#define RESET_BUTTON_GPIO 8
#define VFD_FAULT_DI_GPIO 9
#define CONTACTOR_FEEDBACK_GPIO 10
#define SPARE_DI_GPIO 11
#define BOARD_DI_PINS {4,5,6,7,8,9,10,11}
// DIGITAL OUTPUTS - zero-based expander bits, NOT ESP32 GPIOs
#define VFD_ENABLE_DO_BIT 0
// Proposed lamp wiring; verify the installation before enabling.
#define GREEN_LED_DO_BIT 1
#define YELLOW_LED_DO_BIT 2
#define RED_LED_DO_BIT 3
#define SPARE_DO5_BIT 4
#define SPARE_DO6_BIT 5
#define SPARE_DO7_BIT 6
#define SPARE_DO8_BIT 7
// MODBUS RTU
#define MODBUS_UART_PORT 1
#define MODBUS_TX_PIN 17
#define MODBUS_RX_PIN 18
#define MODBUS_DE_PIN 21
// I2C
#define SENSOR_I2C_SDA_PIN 42
#define SENSOR_I2C_SCL_PIN 41
// ETHERNET
#define ETH_MOSI_PIN 13
#define ETH_MISO_PIN 14
#define ETH_SCLK_PIN 15
#define ETH_CS_PIN 16
#define ETH_INT_PIN 12
#define ETH_RESET_PIN 39
// ONBOARD WS2812
#define BOARD_HEARTBEAT_LED_GPIO 38
