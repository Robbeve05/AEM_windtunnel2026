#pragma once
/* Verified Waveshare pinout; see MIGRATION.md. */
#define MODBUS_UART_PORT 1
#define MODBUS_TX_PIN 17
#define MODBUS_RX_PIN 18
#define MODBUS_DE_PIN 21
#define SENSOR_I2C_SDA_PIN 42
#define SENSOR_I2C_SCL_PIN 41
#define ETH_MOSI_PIN 13
#define ETH_MISO_PIN 14
#define ETH_SCLK_PIN 15
#define ETH_CS_PIN 16
#define ETH_INT_PIN 12
#define ETH_RESET_PIN 39
#define BOARD_DI_PINS {4,5,6,7,8,9,10,11}
#define BOARD_DO_I2C_ADDRESS 0x20
#define BOARD_RTC_I2C_ADDRESS 0x51

/* Functional allocation; field polarity is deliberately uncommissioned. */
#define SAFETY_RELAY_OK_GPIO 4
#define ESTOP_PRESSED_GPIO 5
#define STOP_BUTTON_OK_GPIO 6
#define START_BUTTON_GPIO 7
#define RESET_BUTTON_GPIO 8
#define VFD_FAULT_DI_GPIO 9
#define CONTACTOR_FEEDBACK_GPIO 10
