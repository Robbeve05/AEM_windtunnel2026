#pragma once

/* Voorlopig klassieke ESP32 DevKit. Controleer pinnen voor jouw board. */
#define SENSOR_PERIOD_MS 10
#define ACTUATOR_PERIOD_MS 50
#define SENSOR_TIMEOUT_MS 100
#define CONTROL_CLOCK_TIMEOUT_MS 50
#define COMMAND_TIMEOUT_MS 2000
#define TELEMETRY_PERIOD_MS 200
#define SIMULATION_MODE 1

/* Externe 1 kHz klok naar GPIO4, 3.3 V logic, rising-edge triggered. */
#define CONTROL_CLOCK_GPIO 4
#define CONTROL_CLOCK_HZ 1000
#define CONTROL_CLOCK_PERIOD_S (1.0f / CONTROL_CLOCK_HZ)

/* Pi: UART1, 3.3 V TTL, 115200 8N1. UART0 blijft USB/debug. */
#define PI_UART_PORT 1
#define PI_UART_TX_PIN 17
#define PI_UART_RX_PIN 16
#define PI_UART_BAUD 115200

/* Gereserveerd voor Modbus RTU via externe RS485-transceiver.
 * Drivers blijven bewust niet operationeel totdat slave/registers bekend zijn. */
#define MODBUS_UART_PORT 2
#define MODBUS_TX_PIN 25
#define MODBUS_RX_PIN 26
#define MODBUS_DE_PIN 27
#define MODBUS_SLAVE_ADDRESS 1
#define MODBUS_BAUD 19200
#define SENSOR_I2C_SDA_PIN 21
#define SENSOR_I2C_SCL_PIN 22
#define SENSOR_I2C_HZ 100000

/* Voorbeeldwaarden voor simulatie, niet voor de echte windtunnel afgestemd. */
#define DEFAULT_KP 2.0f
#define DEFAULT_KI 1.0f
#define DEFAULT_KD 0.0f
#define MAX_SETPOINT 30.0f
#define MAX_OUTPUT 100.0f
