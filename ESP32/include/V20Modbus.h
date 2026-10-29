/*
 * V20Modbus.h
 *
 * Non-blocking Siemens SINAMICS V20 Modbus RTU driver for ESP32-S3.
 * Motor control is serviced by ControlTask; no actuator task is required.
 *
 * Author: R.E. van Eekelen
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "TaskData.h"

/* Initialise UART once. Does not transmit a RUN command. */
bool V20Modbus_Init(void);

/*
 * Advance at most one part of an RTU transaction; call once per 5 ms loop.
 * Does not wait for a Modbus response or delay the calling task.
 * safety_ok MUST come from validated physical safety/operational feedback.
 * The request comes from the existing one-element motor_requests queue.
 */
void V20Modbus_Service(const MotorRequest *request, bool safety_ok);

/* Snapshot for the existing one-element motor_status queue. */
void V20Modbus_GetStatus(MotorStatus *status);

/*
 * Call ONLY on an accepted NEW physical Start transition (READY->RUNNING).
 * Returning true merely arms software; it is NOT a safety function.
 */
bool V20Modbus_Arm(void);

/* Inhibit future RUN telegrams and prioritise an OFF1 telegram. */
void V20Modbus_Disarm(void);

/*
 * Request a fresh STOP + read-only commissioning verification.
 * Call only after an explicit Reset and verified healthy safety input.
 * It never clears an actual VFD fault or arms the motor.
 */
void V20Modbus_RequestRecheck(void);

/* Standard Modbus RTU CRC16 (public to facilitate host-side tests). */
uint16_t V20Modbus_Crc16(const uint8_t *data, size_t length);

/* True only after OFF1 was acknowledged and a subsequent fresh speed report
 * confirms the drive below the threshold. No safety-rated standstill claim. */
bool V20Modbus_StopConfirmed(int64_t stop_entered_us, float maximum_hz);
