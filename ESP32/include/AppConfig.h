#pragma once
#include "BoardConfig.h"
#define CONTROL_FREQUENCY_HZ 1000
#define SENSOR_FREQUENCY_HZ 25
#define MODBUS_FREQUENCY_HZ 10
#define SENSOR_PERIOD_MS (1000 / SENSOR_FREQUENCY_HZ)
#define ACTUATOR_PERIOD_MS (1000 / MODBUS_FREQUENCY_HZ)
#define SENSOR_TIMEOUT_MS 150
#define CONTROL_CLOCK_TIMEOUT_MS 50
#define COMMAND_TIMEOUT_MS 2000
#define TELEMETRY_PERIOD_MS 20
#ifndef SIMULATION_MODE
#define SIMULATION_MODE 0
#endif
#define CONTROL_CLOCK_HZ CONTROL_FREQUENCY_HZ
#define CONTROL_CLOCK_PERIOD_S (1.0f / CONTROL_CLOCK_HZ)
#define MODBUS_SLAVE_ADDRESS 1
#define MODBUS_BAUD 9600
#define MODBUS_PARITY UART_PARITY_EVEN
#define MODBUS_STOP_BITS UART_STOP_BITS_1
#define MODBUS_RESPONSE_TIMEOUT_MS 80
#define MOTOR_STATUS_TIMEOUT_MS 600
#define SENSOR_I2C_HZ 100000
#define I2C_TIMEOUT_MS 10
#define PI_TCP_PORT 5000
#define PITOT_ADDRESS 0x28
#define AIR_DENSITY 1.225f
#define PITOT_MIN_PA (-6894.757f)
#define PITOT_MAX_PA 6894.757f
#define PITOT_ZERO_SAMPLES 200
#define PITOT_FILTER_HZ 1.0f
#define PITOT_DEADBAND_PA 5.0f
#ifndef PRESSURE_TAPS_ENABLED
#define PRESSURE_TAPS_ENABLED 0
#endif
#define MUX_0_ADDRESS 0x70
#define MUX_1_ADDRESS 0x71
#ifndef ABP2_ADDRESS
#define ABP2_ADDRESS 0x28
#endif
#if MUX_0_ADDRESS == MUX_1_ADDRESS || MUX_0_ADDRESS < 0x70 || MUX_0_ADDRESS > 0x77 || MUX_1_ADDRESS < 0x70 || MUX_1_ADDRESS > 0x77
#error Invalid or duplicate TCA9548A addresses
#endif
#if PRESSURE_TAPS_ENABLED
#if ABP2_ADDRESS == PITOT_ADDRESS
#error ABP2 address conflicts with upstream pitot; order a distinct I2C address or revise PCB topology
#endif
#ifndef ABP2_MIN_PA
#error Specify ABP2_MIN_PA, ABP2_MAX_PA, ABP2_OUTPUT_MIN and ABP2_OUTPUT_MAX from ordered part
#endif
#endif
/* PI gains: Regelaar_INIT. */
#define MAX_SETPOINT 30.0f
#define MAX_OUTPUT 100.0f

/* Vijfdegraads luchtsnelheid-setpointcurve. Zelf instelbaar.
 * Gevraagde totale overgangstijd; automatisch langer als limieten dat vereisen.
 * Voorlopige waarden, geen gemeten grenzen van de installatie. */
#define SETPOINT_TRANSITION_TIME_S 5.0f
#define SETPOINT_MAX_ACCELERATION_MPS2 2.0f
#define SETPOINT_MAX_JERK_MPS3 2.0f

/* Optional operational DI interlock: -1 = unassigned; DI index 0..7 otherwise.
 * Verify actual wiring/polarity before assigning. Never replaces hardware E-stop. */
#define INTERLOCK_DI_INDEX (-1)
#define INTERLOCK_OK_LEVEL 0
