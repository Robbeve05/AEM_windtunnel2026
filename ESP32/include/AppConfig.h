/* Author: R.E. van Eekelen */
#pragma once
#include "BoardConfig.h"
#ifdef __has_include
#if __has_include("OtaLocalConfig.h")
#include "OtaLocalConfig.h"
#endif
#endif
#define CONTROL_FREQUENCY_HZ 200
#define SENSOR_FREQUENCY_HZ 20
#define PITOT_AVERAGE_SAMPLES 10
#define SENSOR_PERIOD_MS (1000 / SENSOR_FREQUENCY_HZ)
#define SENSOR_TIMEOUT_MS 150
#define TAP_TIMEOUT_MS 2000
#define CONTROL_CLOCK_TIMEOUT_MS 50
#define COMMAND_TIMEOUT_MS 2000
#define TELEMETRY_PERIOD_MS 20
#ifndef SIMULATION_MODE
#define SIMULATION_MODE 0
#endif
#define CONTROL_CLOCK_HZ CONTROL_FREQUENCY_HZ
#define CONTROL_CLOCK_PERIOD_S (1.0f / CONTROL_CLOCK_HZ)
#define MODBUS_SLAVE_ADDRESS 1
#define MODBUS_BAUD 115200
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
/* The existing PI and quintic ramp parameters are retained. */
#define MAX_SETPOINT 30.0f
#define MAX_OUTPUT 100.0f
#define SETPOINT_TRANSITION_TIME_S 5.0f
#define SETPOINT_MAX_ACCELERATION_MPS2 2.0f
#define SETPOINT_MAX_JERK_MPS3 2.0f
#define CONTROL_PERIOD_US 5000u
#define V20_SERVICE_BUDGET_US 1000u
#define VFD_STATUS_PERIOD_US 100000u
#define MODBUS_SILENCE_US 1750u
#define MODBUS_WATCHDOG_MS 500u
#define INPUT_TIMEOUT_MS 100u
#define BUTTON_DEBOUNCE_MS 30u
#define RESET_FEEDBACK_WINDOW_MS 5000u
#define INIT_TIMEOUT_MS 15000u
#define NORMAL_STOP_TIMEOUT_MS 30000u
#define STOP_CONFIRM_HZ 0.1f
#ifndef SAFETY_IO_COMMISSIONED
#define SAFETY_IO_COMMISSIONED 0
#endif
#ifndef SAFETY_RELAY_OK_ACTIVE_LEVEL
#define SAFETY_RELAY_OK_ACTIVE_LEVEL (-1)
#endif
#ifndef ESTOP_PRESSED_ACTIVE_LEVEL
#define ESTOP_PRESSED_ACTIVE_LEVEL (-1)
#endif
#ifndef STOP_BUTTON_OK_ACTIVE_LEVEL
#define STOP_BUTTON_OK_ACTIVE_LEVEL (-1)
#endif
#ifndef START_BUTTON_ACTIVE_LEVEL
#define START_BUTTON_ACTIVE_LEVEL (-1)
#endif
#ifndef RESET_BUTTON_ACTIVE_LEVEL
#define RESET_BUTTON_ACTIVE_LEVEL (-1)
#endif
#ifndef VFD_FAULT_DI_FITTED
#define VFD_FAULT_DI_FITTED 0
#endif
#ifndef VFD_FAULT_DI_ACTIVE_LEVEL
#define VFD_FAULT_DI_ACTIVE_LEVEL (-1)
#endif
#ifndef CONTACTOR_FEEDBACK_FITTED
#define CONTACTOR_FEEDBACK_FITTED 0
#endif
#ifndef CONTACTOR_OK_ACTIVE_LEVEL
#define CONTACTOR_OK_ACTIVE_LEVEL (-1)
#endif
#ifndef VFD_ENABLE_ACTIVE_LEVEL
#define VFD_ENABLE_ACTIVE_LEVEL 1 /* Waveshare TCA9554 EXIO1 high = DO1 active; K2 remains inhibited until commissioning */
#endif
#ifndef PI_HEARTBEAT_REQUIRED
#define PI_HEARTBEAT_REQUIRED 1
#endif
#if defined(ALLOW_REMOTE_START) && ALLOW_REMOTE_START
#error Remote START is prohibited; use the physical button
#endif
#ifndef ALLOW_REMOTE_START
#define ALLOW_REMOTE_START 0
#endif
#ifndef ETH_STATIC_IP_ENABLED
#define ETH_STATIC_IP_ENABLED 0
#endif
#ifndef ETH_STATIC_IP
#define ETH_STATIC_IP "192.168.50.10"
#endif
#ifndef ETH_STATIC_MASK
#define ETH_STATIC_MASK "255.255.255.0"
#endif
#ifndef ETH_STATIC_GATEWAY
#define ETH_STATIC_GATEWAY "192.168.50.1"
#endif
#if SAFETY_IO_COMMISSIONED && (SAFETY_RELAY_OK_ACTIVE_LEVEL < 0 || ESTOP_PRESSED_ACTIVE_LEVEL < 0 || STOP_BUTTON_OK_ACTIVE_LEVEL < 0 || START_BUTTON_ACTIVE_LEVEL < 0 || RESET_BUTTON_ACTIVE_LEVEL < 0 || VFD_ENABLE_DO_BIT < 0 || VFD_ENABLE_DO_BIT > 7 || VFD_ENABLE_ACTIVE_LEVEL < 0)
#error Commission all input levels and the non-safety K2 output before enabling hardware
#endif

#if SAFETY_IO_COMMISSIONED && (SAFETY_RELAY_OK_ACTIVE_LEVEL > 1 || ESTOP_PRESSED_ACTIVE_LEVEL > 1 || STOP_BUTTON_OK_ACTIVE_LEVEL > 1 || START_BUTTON_ACTIVE_LEVEL > 1 || RESET_BUTTON_ACTIVE_LEVEL > 1 || VFD_ENABLE_ACTIVE_LEVEL > 1)
#error Active levels must be zero or one
#endif
#if VFD_FAULT_DI_FITTED && (VFD_FAULT_DI_ACTIVE_LEVEL < 0 || VFD_FAULT_DI_ACTIVE_LEVEL > 1)
#error Commission the optional VFD fault polarity before fitting it
#endif
#if CONTACTOR_FEEDBACK_FITTED && (CONTACTOR_OK_ACTIVE_LEVEL < 0 || CONTACTOR_OK_ACTIVE_LEVEL > 1)
#error Commission the optional contactor feedback polarity before fitting it
#endif

#define V20_REFERENCE_HZ 50.0f
#define MANUAL_MAX_HZ 50.0f
#define MANUAL_MAX_SLEW_HZ_PER_S 2.0f
#define MANUAL_MAX_ACCEL_HZ_PER_S2 0.5f
#define MANUAL_MAX_JERK_HZ_PER_S3 0.5f
#define SENSOR_PHASE_US 2500u
#ifndef OTA_MAINTENANCE_ENABLED
#define OTA_MAINTENANCE_ENABLED 0
#endif
#ifndef OTA_MAINTENANCE_GPIO
#define OTA_MAINTENANCE_GPIO (-1)
#endif
#ifndef OTA_MAINTENANCE_ACTIVE_LEVEL
#define OTA_MAINTENANCE_ACTIVE_LEVEL (-1)
#endif
#ifndef OTA_MOTOR_ISOLATED_GPIO
#define OTA_MOTOR_ISOLATED_GPIO (-1)
#endif
#ifndef OTA_MOTOR_ISOLATED_ACTIVE_LEVEL
#define OTA_MOTOR_ISOLATED_ACTIVE_LEVEL (-1)
#endif
#ifndef OTA_SHARED_SECRET
#define OTA_SHARED_SECRET ""
#endif
#if OTA_MAINTENANCE_ENABLED && (OTA_MAINTENANCE_GPIO < 0 || OTA_MOTOR_ISOLATED_GPIO < 0 || OTA_MAINTENANCE_ACTIVE_LEVEL < 0 || OTA_MOTOR_ISOLATED_ACTIVE_LEVEL < 0)
#error OTA requires two wired physical inputs
#endif
#if OTA_MAINTENANCE_ENABLED
_Static_assert(sizeof(OTA_SHARED_SECRET) >= 33, "OTA secret must contain at least 32 bytes");
#endif

#if OTA_MAINTENANCE_ENABLED
#if !CONTACTOR_FEEDBACK_FITTED || !SAFETY_IO_COMMISSIONED
#error OTA requires commissioned independent contactor and safety feedback
#endif
#if OTA_MAINTENANCE_GPIO == OTA_MOTOR_ISOLATED_GPIO || OTA_MAINTENANCE_ACTIVE_LEVEL > 1 || OTA_MOTOR_ISOLATED_ACTIVE_LEVEL > 1
#error OTA requires distinct physical inputs and binary polarities
#endif

#endif

// I2C device configuration (not physical pin assignments).
#define BOARD_DO_I2C_ADDRESS 0x20
#define BOARD_RTC_I2C_ADDRESS 0x51
// EXTERNAL STATUS LAMPS - proposed wiring, disabled until verified.
#ifndef EXTERNAL_STATUS_LEDS_ENABLED
#define EXTERNAL_STATUS_LEDS_ENABLED 0
#endif
#ifndef STATUS_LED_ACTIVE_LEVEL
#define STATUS_LED_ACTIVE_LEVEL 1
#endif
#define STATUS_LED_BLINK_PERIOD_MS 1000u
#define STATUS_LED_UPDATE_PERIOD_MS 100u
#define STATUS_RGB_TX_TIMEOUT_MS 25u

/*
 * Deliberate commissioning inhibit. Keep at zero until the real relay,
 * digital-input polarity, OFF2 hardware path and Modbus V20 settings have
 * been validated with the motor unable to start unexpectedly.
 */
#ifndef V20_RUN_COMMISSIONED
#define V20_RUN_COMMISSIONED 0
#endif

#ifndef V20_STATUS_PERIOD_MS
#define V20_STATUS_PERIOD_MS 100
#endif
#ifndef V20_COMMAND_PERIOD_MS
#define V20_COMMAND_PERIOD_MS 5
#endif
#ifndef V20_STOP_HEARTBEAT_MS
#define V20_STOP_HEARTBEAT_MS 150
#endif
#ifndef V20_EXPECTED_WATCHDOG_MS
#define V20_EXPECTED_WATCHDOG_MS 500
#endif
#ifndef V20_EXPECTED_REFERENCE_CENTIHZ
#define V20_EXPECTED_REFERENCE_CENTIHZ 5000
#endif
#ifndef V20_RESPONSE_TIMEOUT_MS
#define V20_RESPONSE_TIMEOUT_MS MODBUS_RESPONSE_TIMEOUT_MS
#endif


/* Existing controller design assumptions, moved unchanged (not measurements). */
#define LUCHTSNELHEID_NOM    (40.0f)  // m/s at reference frequency
#define FREQUENTIE_NOM      (50.0f)  // Hz, corresponding to 100% output
#define Tau_motor           (0.6f)   // s, assumed VFD/fan time constant
#define FbM                 (0.20f)  // Hz, target bandwidth
#define PI_ZERO_DIVISOR     (10.0f)  // PI zero one decade below bandwidth
