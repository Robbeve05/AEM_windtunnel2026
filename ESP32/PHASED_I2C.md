# Phased I2C acquisition: Core 1 pitot and Core 0 pressure taps

Author: R.E. van Eekelen

## Design

A **single GPTimer** emits an interrupt every **2,500 us** (400 Hz), and alternates the target notification:

| GPTimer alarm | Task notified | Work |
| --- | --- | --- |
| Phase A: t = 2.5, 7.5, 12.5 ms... | Core 1, ControlTask | Pitot read, PI, state machine and V20 service |
| Phase B: t = 5, 10, 15 ms... | Core 0, SensorTask | ONE pressure-tap I2C operation |
 
Both task notifications therefore have a period of **5 ms (200 Hz)**, offset by 2.5 ms. The ISR only notifies tasks; it NEVER accesses I2C.

The pitot **alone** is sampled inside ControlTask, one four-byte transaction per 5 ms tick. Ten successful readings form a batch average, published in the existing one-element `sensors` queue at approximately **20 Hz**. ControlTask reuses the most recent sample between publications. A one-off bus contention does not reset the batch; more than `SENSOR_TIMEOUT_MS` without a new valid sample makes the sensor unhealthy.

The sixteen ABP2 wing-tap channels stay in SensorTask on Core 0. `SensorLib_TapsStep()` executes one transaction per notification, progressing through: deselect mux0, deselect mux1, select channel, trigger conversion, wait for ready in later passes, read data, deselect mux0, deselect mux1. A complete scan may take **over half a second**: it is deliberately slower than the pitot and is **not a 25 Hz all-channel scan**. Each channel's latest reading is published via `tap_samples` queue.

In `PiCommunicationTask` on Core 0, pitot data is obtained by `xQueuePeek(sensors)`, and wing taps separately by `xQueuePeek(tap_samples)`. This is independent of the Core 1 `xQueuePeek(sensors)` controller use. The Pi uses `sample_seq` so 50 Hz telemetry does not record the same 20 Hz pitot sample repeatedly.

## Important timing limitation

The offset is a **notification schedule**, not an absolute I2C hardware reservation. FreeRTOS scheduling, SafetyTask, interrupt latency and the I2C hardware may shift execution. A FreeRTOS mutex still protects the bus, and the Core 1 sensor uses a non-waiting mutex attempt with a short I2C timeout. Never interrupt/reset I2C hardware to grab the bus in the control ISR.

At the default `SENSOR_I2C_HZ = 100000`, a four-byte read is typically sub-millisecond on an idle bus but real latency is NOT established without board measurements. For strict hard-real-time acquisition, put the pitot on a physically separate I2C controller with its own SDA/SCL pins.

`PRESSURE_TAPS_ENABLED=0` is the default. Do not turn it on until the actual ABP2 address (different from the root pitot 0x28), pressure limits and wing-tap PCB are verified.

## Validation before motor enable

1. Build `pio run -e waveshare_s3_sim` and `pio run -e waveshare_s3`.
2. Verify `control_hz` near **200 Hz**, `control_period_us = 5000`, and no increasing `missed_deadlines` or `overruns`.
3. Confirm `sample_seq` increases around **20 times/s** after pitot zero calibration and `pv` reaches the Raspberry Pi through TCP.
4. With taps enabled on a dedicated test PCB, check their incremental scan never causes stale pitot data or overruns, even while the safety task operates its I2C DO.
5. Disconnect the pitot: `sensor_ok` must become false when the freshness timeout expires; the state machine must fault when active.
6. Keep `SAFETY_IO_COMMISSIONED` and `V20_RUN_COMMISSIONED` disabled until independent physical safety and VFD checks pass.

These tests have NOT been performed on real hardware. This is operational control software, not a safety-related control function.
