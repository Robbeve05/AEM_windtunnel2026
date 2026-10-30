/* Author: R.E. van Eekelen */
// Configuration defaults; classifications identify settings requiring validation.
#pragma once
#include "BoardConfig.h"
#ifdef __has_include
#if __has_include("OtaLocalConfig.h")
#include "OtaLocalConfig.h"
#endif
#endif

///////////////////////////////////////////////////////////////////////////////
// 01. GENERAL SYSTEM CONFIGURATION
///////////////////////////////////////////////////////////////////////////////
// System-wide operating mode. Physical GPIO and DO assignments belong in
// MachinePins.h (included through BoardConfig.h), not in this file.

#ifndef SIMULATION_MODE
// [Fixed build flag] 0 uses hardware; 1 selects simulated operation for testing.
#define SIMULATION_MODE 0
#endif

///////////////////////////////////////////////////////////////////////////////
// 02. FREERTOS & CONTROL TASK TIMING
///////////////////////////////////////////////////////////////////////////////
// Control clock and sensor phase timing; keep the period and frequency consistent.
// Task priorities and task allocation are defined elsewhere and are unchanged.

// [Hz, fixed] Control-loop rate; changing it changes sampling and requires timing validation.
#define CONTROL_FREQUENCY_HZ 200

// [Hz, fixed derived] Control clock follows CONTROL_FREQUENCY_HZ; keep them synchronized.
#define CONTROL_CLOCK_HZ CONTROL_FREQUENCY_HZ

// [s, fixed derived] Controller sample interval; a higher clock rate reduces this interval.
#define CONTROL_CLOCK_PERIOD_S (1.0f / CONTROL_CLOCK_HZ)

// [us, fixed] Control deadline period; must agree with CONTROL_FREQUENCY_HZ.
#define CONTROL_PERIOD_US 5000u

// [us, fixed] Half-period clock phase for sensor/control scheduling; must remain half the
// control period.
#define SENSOR_PHASE_US 2500u

// [ms, safety-critical] Maximum wait for the control clock; increasing delays clock-failure
// detection.
#define CONTROL_CLOCK_TIMEOUT_MS 50

// [us, fixed] V20 service execution budget; increasing leaves less time within the control
// deadline.
#define V20_SERVICE_BUDGET_US 1000u

///////////////////////////////////////////////////////////////////////////////
// 03. SIEMENS V20 MODBUS COMMUNICATION
///////////////////////////////////////////////////////////////////////////////
// RTU framing, service cadence and drive verification. Match settings to the V20.
// Retained legacy timing constants are documented even where no source uses them.

// [Fixed] RTU slave ID; changing it targets a different drive address.
#define MODBUS_SLAVE_ADDRESS 1

// [bit/s, fixed] UART baud rate; a mismatch prevents communication with the V20.
#define MODBUS_BAUD 115200

// [Fixed] UART parity; must match the drive or frames will be rejected.
#define MODBUS_PARITY UART_PARITY_EVEN

// [Fixed] UART stop-bit format; must match the drive framing.
#define MODBUS_STOP_BITS UART_STOP_BITS_1

// [ms, adjustable] Default response wait; increasing tolerates slower replies but delays
// timeout detection.
#define MODBUS_RESPONSE_TIMEOUT_MS 80

// [ms, safety-critical] Maximum status age; increasing permits older drive feedback.
#define MOTOR_STATUS_TIMEOUT_MS 600

// [us, fixed legacy] Retained status-period setting; currently unused by production sources.
#define VFD_STATUS_PERIOD_US 100000u

// [us, fixed] RTU inter-frame silence; reducing risks frame-boundary violations.
#define MODBUS_SILENCE_US 1750u

// [ms, fixed legacy] Retained watchdog setting; currently unused by production sources.
#define MODBUS_WATCHDOG_MS 500u

#ifndef V20_STATUS_PERIOD_MS
// [ms, adjustable] Status polling interval; reducing increases bus traffic and feedback freshness.
#define V20_STATUS_PERIOD_MS 100
#endif

#ifndef V20_COMMAND_PERIOD_MS
// [ms, adjustable] Running command interval; reducing increases command traffic.
#define V20_COMMAND_PERIOD_MS 5
#endif

#ifndef V20_STOP_HEARTBEAT_MS
// [ms, safety-critical] Stopped command heartbeat interval; increasing reduces watchdog margin.
#define V20_STOP_HEARTBEAT_MS 150
#endif

#ifndef V20_EXPECTED_WATCHDOG_MS
// [ms, safety-critical] Expected V20 watchdog register value; mismatch fails drive verification.
#define V20_EXPECTED_WATCHDOG_MS 500
#endif

#ifndef V20_EXPECTED_REFERENCE_CENTIHZ
// [0.01 Hz, fixed] Expected drive reference register value; mismatch fails verification (50 Hz
// required).
#define V20_EXPECTED_REFERENCE_CENTIHZ 5000
#endif

#ifndef V20_RESPONSE_TIMEOUT_MS
// [ms, adjustable] Driver response timeout, defaults to the RTU timeout; increasing delays
// failed-reply detection.
#define V20_RESPONSE_TIMEOUT_MS MODBUS_RESPONSE_TIMEOUT_MS
#endif

///////////////////////////////////////////////////////////////////////////////
// 04. MOTOR & FAN SPECIFICATIONS
///////////////////////////////////////////////////////////////////////////////
// Installed fan: Ruck EL 710 D4 02, article 146531.
// Earlier softstarter testing measured approximately 40 m/s at 50 Hz; this is
// not a guaranteed operating point. See section 07 for the preliminary model.
// No additional nameplate values are assumed here.

// [Hz, fixed] Reference for Modbus frequency scaling; must match the V20 reference or demands
// scale incorrectly.
#define V20_REFERENCE_HZ 50.0f

///////////////////////////////////////////////////////////////////////////////
// 05. MANUAL FREQUENCY CONTROL
///////////////////////////////////////////////////////////////////////////////
// MANUAL commands motor frequency directly through the V20. Its fifth-order
// SetpointCurve limits frequency derivatives independently of the automatic
// airspeed curve; it does not regulate measured airspeed.

// [Hz, safety-critical] Maximum manual frequency; raising it requires validated motor and fan
// operating limits.
#define MANUAL_MAX_HZ 50.0f

// [Hz/s, adjustable] Maximum first derivative of frequency; lowering slows manual transitions.
#define MANUAL_MAX_SLEW_HZ_PER_S 2.0f

// [Hz/s^2, adjustable] Maximum second derivative of frequency; lowering smooths changes in
// frequency slope.
#define MANUAL_MAX_ACCEL_HZ_PER_S2 0.5f

// [Hz/s^3, adjustable] Maximum third derivative of frequency; lowering further
// lengthens/smooths manual transitions.
#define MANUAL_MAX_JERK_HZ_PER_S3 0.5f

///////////////////////////////////////////////////////////////////////////////
// 06. AUTOMATIC AIRSPEED CONTROL
///////////////////////////////////////////////////////////////////////////////
// AUTO shapes an airspeed setpoint in m/s with a fifth-order SetpointCurve,
// then uses the PI controller to determine motor demand. These derivative
// limits apply to airspeed, not to the manual frequency trajectory.

// [m/s, safety-critical] Maximum requested airspeed; raising it requires validated tunnel
// operating limits.
#define MAX_SETPOINT 30.0f

// [%, safety-critical] Controller output ceiling used when applying PID settings; raising
// permits higher motor demand.
#define MAX_OUTPUT 100.0f

// [s, adjustable] Requested minimum airspeed transition duration; derivative limits may extend it.
#define SETPOINT_TRANSITION_TIME_S 5.0f

// [m/s^2, adjustable] Maximum airspeed slope (dv/dt); lowering slows automatic setpoint changes.
#define SETPOINT_MAX_ACCELERATION_MPS2 2.0f

// [m/s^3, adjustable] Maximum airspeed second derivative; lowering smooths changes in airspeed
// slope.
#define SETPOINT_MAX_JERK_MPS3 2.0f

///////////////////////////////////////////////////////////////////////////////
// 07. PI CONTROLLER & FAN MODEL
///////////////////////////////////////////////////////////////////////////////
// Existing first-order model and PI design inputs, preserved without retuning.
// The measured reference below is distinct from assumed dynamic response.
// Changing these inputs changes the gains calculated by the existing controller.

// [m/s, measured reference] Approximately 40 m/s in earlier softstarter testing; not guaranteed
// at 50 Hz. Sets model gain.
#define LUCHTSNELHEID_NOM    (40.0f)

// [Hz, preliminary model reference] Frequency corresponding to nominal airspeed and 100%
// output; changes model/scaling gain.
#define FREQUENTIE_NOM      (50.0f)

// [s, preliminary engineering assumption] First-order motor/fan time constant; NOT
// experimentally identified. Changes PI tuning.
#define Tau_motor           (0.6f)

// [Hz, preliminary design target] Closed-loop bandwidth used in the existing PI design;
// increasing requests faster response.
#define FbM                 (0.20f)

// [dimensionless, preliminary design choice] Bandwidth-to-PI-zero ratio; increasing places the
// PI zero at a lower frequency.
#define PI_ZERO_DIVISOR     (10.0f)
// 07A. THEORETICAL ONE-MASS CONTROLLER DESIGN (NOT V20 GAINS)
// Enter actual shaft inertias independently; zero means unknown/not configured.
#define J_MOTOR_KGM2 (0.0f)
#define J_FAN_KGM2 (0.0f)
// Additive airspeed output disturbance specification.
#define DISTURBANCE_FREQUENCY_HZ (0.02f)
#define DISTURBANCE_AMPLITUDE_MS (5.0f)
#define ALLOWABLE_ERROR_MS (0.5f)
// Nominal speed for local angular-to-airspeed conversion.
#define DESIGN_NOMINAL_RPM (1450.0f)
#define DESIGN_MIN_PHASE_MARGIN_DEG (45.0f)
// Theoretical torque-model gains must not command the frequency-controlled V20.
#define THEORETICAL_PID_DEPLOYMENT (0)
// 07B. FREQUENCY FEEDFORWARD (AIRSPEED -> V20 OUTPUT PERCENT)
// Disabled until the static 50-Hz / 40-m/s mapping and closed-loop behavior
// have been measured. Enabling changes control action; bench-test first.
// [0/1] Feedforward sums a reference-based command with the PID correction.
#define V20_FEEDFORWARD_ENABLED (0)
// [Hz per (m/s)] Static feedforward from reference airspeed to V20 frequency.
// This preliminary approximation uses the nominal point, not a fitted fan curve.
#define V20_FF_STATIC_HZ_PER_MS (FREQUENTIE_NOM / LUCHTSNELHEID_NOM)
// [Hz per (m/s2)] Dynamic feedforward for airspeed reference acceleration.
// Default 0: inertia-derived torque cannot be converted to Hz without a
// validated VFD/motor torque-slip model.
#define V20_FF_ACCEL_HZ_PER_MS2 (0.0f)
#if V20_FEEDFORWARD_ENABLED != 0 && V20_FEEDFORWARD_ENABLED != 1
#error V20_FEEDFORWARD_ENABLED must be 0 or 1
#endif

#if THEORETICAL_PID_DEPLOYMENT != 0
#error Theoretical torque PID must not be deployed to V20
#endif


///////////////////////////////////////////////////////////////////////////////
// 08. PITOT SENSOR
///////////////////////////////////////////////////////////////////////////////
// Pressure conversion, averaging and zero calibration for the upstream pitot.
// Pressure range must match the installed sensor; filtering adds measurement lag.

// [Hz, fixed legacy] Nominal sensor reporting rate used to derive SENSOR_PERIOD_MS; acquisition
// uses the control clock.
#define SENSOR_FREQUENCY_HZ 20

// [ms, fixed derived legacy] Nominal reporting period; currently unused by production sources.
#define SENSOR_PERIOD_MS (1000 / SENSOR_FREQUENCY_HZ)

// [samples, adjustable] Raw samples per averaging window; increasing reduces noise and slows
// measurement updates.
#define PITOT_AVERAGE_SAMPLES 10

// [ms, safety-critical] Maximum pitot sample age; increasing allows older airspeed feedback.
#define SENSOR_TIMEOUT_MS 150

// [7-bit I2C address, fixed] Upstream pitot device address; changing selects a different device.
#define PITOT_ADDRESS 0x28

// [kg/m^3, preliminary engineering assumption] Density for pressure-to-speed conversion; higher
// density gives lower calculated speed.
#define AIR_DENSITY 1.225f

// [Pa, fixed] Sensor lower pressure endpoint; changing alters calibrated pressure conversion.
#define PITOT_MIN_PA (-6894.757f)

// [Pa, fixed] Sensor upper pressure endpoint; changing alters calibrated pressure conversion.
#define PITOT_MAX_PA 6894.757f

// [samples, adjustable] Samples accumulated for zero calibration; increasing extends
// calibration averaging time.
#define PITOT_ZERO_SAMPLES 200

// [Hz, adjustable] Default low-pass cutoff; increasing responds faster but passes more noise.
#define PITOT_FILTER_HZ 1.0f

// [Pa, adjustable] Near-zero pressure deadband; increasing suppresses more small pressure readings.
#define PITOT_DEADBAND_PA 5.0f

///////////////////////////////////////////////////////////////////////////////
// 09. PRESSURE TAP SENSORS
///////////////////////////////////////////////////////////////////////////////
// Optional ABP2 sensors behind TCA9548A multiplexers. Enabling requires the
// ordered sensor range/output calibration and a non-conflicting I2C address.

#ifndef PRESSURE_TAPS_ENABLED
// [Fixed feature flag] 0 disables tap acquisition; 1 enables it after address/calibration
// validation.
#define PRESSURE_TAPS_ENABLED 0
#endif

// [ms, adjustable] Maximum pressure-tap data age; increasing tolerates older tap readings.
#define TAP_TIMEOUT_MS 2000

// [7-bit I2C address, fixed] First multiplexer address; must match wiring and differ from
// MUX_1_ADDRESS.
#define MUX_0_ADDRESS 0x70

// [7-bit I2C address, fixed] Second multiplexer address; must match wiring and differ from
// MUX_0_ADDRESS.
#define MUX_1_ADDRESS 0x71

#ifndef ABP2_ADDRESS
// [7-bit I2C address, fixed] Tap sensor address; enabling taps requires an address distinct
// from the upstream pitot.
#define ABP2_ADDRESS 0x28
#endif

///////////////////////////////////////////////////////////////////////////////
// 10. I2C COMMUNICATION
///////////////////////////////////////////////////////////////////////////////
// Shared bus speed, transaction timeout and onboard device addresses.
// Physical SDA/SCL pin assignments remain in MachinePins.h.

// [Hz, fixed] I2C bus clock; increasing shortens transfers but requires all devices/wiring to
// support it.
#define SENSOR_I2C_HZ 100000

// [ms, adjustable] Bus transaction timeout; increasing can block longer on an unresponsive device.
#define I2C_TIMEOUT_MS 10

// [7-bit I2C address, fixed] Output expander address; changing selects a different bus device.
#define BOARD_DO_I2C_ADDRESS 0x20

// [7-bit I2C address, fixed] RTC device address; changing must match the installed hardware.
#define BOARD_RTC_I2C_ADDRESS 0x51

///////////////////////////////////////////////////////////////////////////////
// 11. PHYSICAL INPUT POLARITIES
///////////////////////////////////////////////////////////////////////////////
// Safety-critical commissioning values: -1 means unknown/unverified, 0 means
// active low, and 1 means active high. Physically measure each input before
// setting its level. Commissioning flags are grouped in section 13.

#ifndef SAFETY_RELAY_OK_ACTIVE_LEVEL
// [Logic level, safety-critical] Level indicating healthy safety relay feedback; wrong polarity
// misinterprets relay state.
#define SAFETY_RELAY_OK_ACTIVE_LEVEL (-1)
#endif

#ifndef ESTOP_PRESSED_ACTIVE_LEVEL
// [Logic level, safety-critical] Level indicating a pressed emergency stop; wrong polarity
// reverses its software indication.
#define ESTOP_PRESSED_ACTIVE_LEVEL (-1)
#endif

#ifndef STOP_BUTTON_OK_ACTIVE_LEVEL
// [Logic level, safety-critical] Level indicating the stop circuit is healthy; wrong polarity
// reverses the stop permissive.
#define STOP_BUTTON_OK_ACTIVE_LEVEL (-1)
#endif

#ifndef START_BUTTON_ACTIVE_LEVEL
// [Logic level, safety-critical] Level indicating physical START; wrong polarity misinterprets
// button activation.
#define START_BUTTON_ACTIVE_LEVEL (-1)
#endif

#ifndef RESET_BUTTON_ACTIVE_LEVEL
// [Logic level, safety-critical] Level indicating physical RESET; wrong polarity misinterprets
// reset requests.
#define RESET_BUTTON_ACTIVE_LEVEL (-1)
#endif

#ifndef VFD_FAULT_DI_ACTIVE_LEVEL
// [Logic level, safety-critical] Level indicating optional drive fault; verify before enabling
// the fitted flag.
#define VFD_FAULT_DI_ACTIVE_LEVEL (-1)
#endif

#ifndef CONTACTOR_OK_ACTIVE_LEVEL
// [Logic level, safety-critical] Level indicating optional contactor feedback is healthy;
// verify before enabling its fitted flag.
#define CONTACTOR_OK_ACTIVE_LEVEL (-1)
#endif

///////////////////////////////////////////////////////////////////////////////
// 12. OUTPUT CONFIGURATION
///////////////////////////////////////////////////////////////////////////////
// Operational K2 permissive output polarity; physical DO bit is in MachinePins.h.
// Software GPIO control is not a certified emergency-stop function.

#ifndef VFD_ENABLE_ACTIVE_LEVEL
// [Logic level, safety-critical] 1 drives EXIO1 high for DO1/K2; 0 inverts it. Physically
// verify; inhibited until commissioning.
#define VFD_ENABLE_ACTIVE_LEVEL 1
#endif

///////////////////////////////////////////////////////////////////////////////
// 13. SAFETY & COMMISSIONING
///////////////////////////////////////////////////////////////////////////////
// A commissioning flag of 0 means NOT commissioned. Never enable these flags
// automatically. Verify real relay/input polarity, OFF2 hardware path and V20
// settings with the motor unable to start unexpectedly. Physical active levels
// must be measured (section 11); software does not replace a certified E-stop.

#ifndef SAFETY_IO_COMMISSIONED
// [Safety-critical flag] 0 inhibits uncommissioned I/O; 1 permits operation only after physical
// safety I/O verification.
#define SAFETY_IO_COMMISSIONED 0
#endif

#ifndef V20_RUN_COMMISSIONED
// [Safety-critical flag] 0 inhibits drive RUN; 1 permits RUN only after documented
// drive/hardware commissioning.
#define V20_RUN_COMMISSIONED 0
#endif

#ifndef VFD_FAULT_DI_FITTED
// [Safety-critical feature flag] 0 omits optional fault input; 1 uses it and requires a
// verified polarity.
#define VFD_FAULT_DI_FITTED 0
#endif

#ifndef CONTACTOR_FEEDBACK_FITTED
// [Safety-critical feature flag] 0 omits optional feedback; 1 uses independent contactor
// feedback with verified polarity.
#define CONTACTOR_FEEDBACK_FITTED 0
#endif

// [ms, safety-critical] Maximum input snapshot age; increasing delays stale-input fault detection.
#define INPUT_TIMEOUT_MS 100u

// [ms, adjustable] Required stable button time; increasing rejects longer glitches but delays
// button response.
#define BUTTON_DEBOUNCE_MS 30u

// [ms, safety-critical] Allowed feedback recovery window after reset; increasing waits longer
// before failure.
#define RESET_FEEDBACK_WINDOW_MS 5000u

// [ms, safety-critical] Initialization timeout; increasing allows a longer startup before faulting.
#define INIT_TIMEOUT_MS 15000u

// [ms, safety-critical] Maximum normal-stop wait; increasing delays stop-timeout faulting.
#define NORMAL_STOP_TIMEOUT_MS 30000u

// [Hz, safety-critical] Frequency threshold for confirmed stop; increasing accepts more
// residual rotation.
#define STOP_CONFIRM_HZ 0.1f

///////////////////////////////////////////////////////////////////////////////
// 14. RASPBERRY PI COMMUNICATION
///////////////////////////////////////////////////////////////////////////////
// HMI transport, telemetry and heartbeat policy. Remote START is prohibited;
// starting requires the physical button. The prohibition check is in section 18.

// [TCP port, fixed] HMI listening port; changing requires matching Raspberry Pi configuration.
#define PI_TCP_PORT 5000

// [ms, safety-critical] Pi heartbeat freshness limit; increasing delays communication-loss
// detection.
#define COMMAND_TIMEOUT_MS 2000

// [ms, adjustable] Telemetry transmission interval; lowering sends updates more frequently.
#define TELEMETRY_PERIOD_MS 20

#ifndef PI_HEARTBEAT_REQUIRED
// [Safety-critical flag] 1 requires a fresh Pi heartbeat; 0 removes that communication permissive.
#define PI_HEARTBEAT_REQUIRED 1
#endif

#ifndef ALLOW_REMOTE_START
// [Safety-critical fixed flag] Must remain 0; any nonzero override is rejected at compile time.
#define ALLOW_REMOTE_START 0
#endif

///////////////////////////////////////////////////////////////////////////////
// 15. ETHERNET CONFIGURATION
///////////////////////////////////////////////////////////////////////////////
// Addressing for the wired HMI link; match static settings to the local network.

#ifndef ETH_STATIC_IP_ENABLED
// [Adjustable flag] 0 uses dynamic addressing; 1 applies the static address settings below.
#define ETH_STATIC_IP_ENABLED 0
#endif

#ifndef ETH_STATIC_IP
// [IPv4 address, adjustable] Static ESP32 address; changing moves the HMI endpoint when static
// mode is enabled.
#define ETH_STATIC_IP "192.168.50.10"
#endif

#ifndef ETH_STATIC_MASK
// [IPv4 mask, adjustable] Static subnet mask; changing changes which destinations are
// considered local.
#define ETH_STATIC_MASK "255.255.255.0"
#endif

#ifndef ETH_STATIC_GATEWAY
// [IPv4 address, adjustable] Static default gateway; changing changes routing outside the local
// subnet.
#define ETH_STATIC_GATEWAY "192.168.50.1"
#endif

///////////////////////////////////////////////////////////////////////////////
// 16. STATUS LED CONFIGURATION
///////////////////////////////////////////////////////////////////////////////
// External lamps use proposed wiring and remain disabled until verified.
// Physical lamp DO bits and onboard RGB pin remain in MachinePins.h.

#ifndef EXTERNAL_STATUS_LEDS_ENABLED
// [Commissioning feature flag] 0 disables external lamps; set 1 only after physically verifying
// proposed wiring.
#define EXTERNAL_STATUS_LEDS_ENABLED 0
#endif

#ifndef STATUS_LED_ACTIVE_LEVEL
// [Logic level, fixed after verification] 1 energizes lamps with high outputs; 0 inverts lamp
// polarity.
#define STATUS_LED_ACTIVE_LEVEL 1
#endif

// [ms, adjustable] Full status blink cycle; increasing slows blinking.
#define STATUS_LED_BLINK_PERIOD_MS 1000u

// [ms, adjustable] Indicator refresh interval; increasing delays visible status changes.
#define STATUS_LED_UPDATE_PERIOD_MS 100u

// [ms, adjustable] Onboard RGB transmission wait; increasing allows longer blocking on
// transmission.
#define STATUS_RGB_TX_TIMEOUT_MS 25u

///////////////////////////////////////////////////////////////////////////////
// 17. OTA FIRMWARE UPDATES
///////////////////////////////////////////////////////////////////////////////
// OtaLocalConfig.h is optionally included BEFORE all defaults to preserve
// existing override ordering; keep secrets outside Git. OTA requires two verified
// physical maintenance/isolation inputs plus independent commissioned feedback.
// The GPIO defaults below are retained unassigned sentinels, not pin assignments.
// Keep physical assignments in MachinePins.h; never derive these inputs in software.

#ifndef OTA_MAINTENANCE_ENABLED
// [Safety-critical commissioning flag] 0 disables OTA; 1 permits maintenance OTA only after all
// physical prerequisites are verified.
#define OTA_MAINTENANCE_ENABLED 0
#endif

#ifndef OTA_MAINTENANCE_GPIO
// [GPIO sentinel, safety-critical] -1 means unassigned; a verified physical input is required
// to enable OTA.
#define OTA_MAINTENANCE_GPIO (-1)
#endif

#ifndef OTA_MAINTENANCE_ACTIVE_LEVEL
// [Logic level, safety-critical] -1 is unverified; measured 0/1 selects the maintenance input's
// active polarity.
#define OTA_MAINTENANCE_ACTIVE_LEVEL (-1)
#endif

#ifndef OTA_MOTOR_ISOLATED_GPIO
// [GPIO sentinel, safety-critical] -1 means unassigned; OTA requires a distinct verified
// motor-isolation input.
#define OTA_MOTOR_ISOLATED_GPIO (-1)
#endif

#ifndef OTA_MOTOR_ISOLATED_ACTIVE_LEVEL
// [Logic level, safety-critical] -1 is unverified; measured 0/1 selects the isolation input's
// active polarity.
#define OTA_MOTOR_ISOLATED_ACTIVE_LEVEL (-1)
#endif

#ifndef OTA_SHARED_SECRET
// [Secret bytes, safety-critical] Empty disables usable authentication; provision at least 32
// bytes outside Git to authorize OTA.
#define OTA_SHARED_SECRET ""
#endif

///////////////////////////////////////////////////////////////////////////////
// 18. PARAMETER VALIDATION
///////////////////////////////////////////////////////////////////////////////
// Existing address, commissioning, remote-start and OTA checks are preserved.
// All defaults precede validation; invalid enabled configurations fail to build.

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

#if defined(ALLOW_REMOTE_START) && ALLOW_REMOTE_START
#error Remote START is prohibited; use the physical button
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
