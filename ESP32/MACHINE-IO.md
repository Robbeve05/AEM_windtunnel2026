# Central machine I/O and status indication

Author: R.E. van Eekelen

## Logical assignments

MachinePins.h is the sole definition of machine GPIO and zero-based expander bit assignments.
BoardConfig.h is a compatibility include. AppConfig.h owns addresses, polarities, enable and commissioning
flags, all existing controller design parameters and V20 settings; values and motor behavior are unchanged.

| Signal | Terminal | ESP32 GPIO / expander bit | Function |
| --- | --- | --- | --- |
| Safety relay feedback | DI1 | GPIO4 | Safety status |
| Emergency stop | DI2 | GPIO5 | Emergency indication |
| STOP button | DI3 | GPIO6 | Normal stop |
| START button | DI4 | GPIO7 | Physical start |
| RESET button | DI5 | GPIO8 | Physical reset |
| V20 fault | DI6 | GPIO9 | Optional fault feedback |
| K2 feedback | DI7 | GPIO10 | Optional contactor feedback |
| Spare input | DI8 | GPIO11 | Reserved |
| K2 | DO1 | Bit 0 | Existing operational permissive |
| Green lamp | DO2 | Bit 1 | Proposed READY / RUNNING indication |
| Yellow lamp | DO3 | Bit 2 | Proposed INIT / WAIT / STOP indication |
| Red lamp | DO4 | Bit 3 | Proposed FAULT / emergency indication |
| Spare outputs | DO5..DO8 | Bits 4..7 | Reserved, no request API; held low as previously |

The lamp assignments are proposed until installed wiring is verified. This table is a logical signal
assignment, not a complete electrical wiring diagram. DO bits are not ESP32 GPIO numbers or voltage
sources. All existing optically isolated input assignments are retained, as are UART1 TX17/RX18/DE21,
I2C SDA42/SCL41, W5500 MOSI13/MISO14/SCLK15/CS16/INT12/RESET39 and onboard RGB38.

## Output ownership and failure behavior

DigitalOutputs.c maintains independent K2 and lamp requests under a short critical section. A separate
FreeRTOS mutex serializes complete K2-path writes and provides priority inheritance. Only initialization
and DigitalOutputs_Set() write the complete expander image. InputHandlerTask continues to call the same
compatibility API with its existing safety authorization. Neither the heartbeat nor either HMI can authorize K2.
DigitalOutputs_SetStatusLeds() only stages three booleans: it never accesses I2C and never restores a
cached output byte. A request arriving during a write affects lamps on the next normal 10 ms flush.
There are no extra lamp I2C transactions and no new high-priority task, EventGroup or queue.

Initialization creates the shared image with K2 and lamps logically off. The inactive latch is written
BEFORE the existing output directions are enabled. Both K2 and lamp polarities are applied even to the
inactive image; a zero byte is not assumed inactive for active-low settings. Invalid bit ranges, duplicate
assignments and invalid polarity/enable values fail compilation. Mutex or I2C initialization failure leaves
the output module uninitialized; it logs the failure and the existing startup barrier inhibits motor operation.

A failed write returns false to the existing outputs_ok path. DigitalOutputStatus exposes separate requested
and last acknowledged bytes, a confirmed_valid flag and write_errors. A failure clears confirmed_valid;
I2C acknowledgement never proves physical relay or lamp operation. STATUS exposes outputs_ok,
output_write_errors, requested/acknowledged images and output_ack_valid separately. LED staging success only means a
logical request was accepted, never that a physical lamp is on. No new fault or reset policy is introduced.

## Common indication policy

StatusIndicators_Get() remains the sole state-to-color and message policy:

| Condition | Green | Yellow | Red |
| --- | --- | --- | --- |
| INIT | Off | On | On |
| WAIT | Off | Blinking | Off |
| READY | Blinking | Off | Off |
| RUNNING | On | Off | Off |
| STOP | Off | On | Off |
| FAULT | Off | Off | On |
| Emergency pressed | Off | Off | On |
| Emergency released, reset required | Off | Off | Blinking |

Blink period is 1000 ms. HeartbeatTask samples telemetry every 100 ms and uses the same result for
external requests and the existing RMT WS2812 driver. The RGB LED renders simultaneous logical colors
additively; the three external lamps and HMI show the three channels separately. Missing telemetry
requests red/UNKNOWN, never READY. An RMT initialization failure is logged but does not stop the
external status-request loop. These indicators never establish motor standstill and are not safety devices.

PiCommunicationTask publishes led_green, led_yellow, led_red and the existing status text (spaces encoded
as %20) in normal STATUS telemetry. Native and browser HMI consume those flags rather than implementing
another color policy or local blink table. Missing/disconnected status shows UNKNOWN/gray. No new
lamp command or manual fault-indication override exists. HMI indication works with external lamps disabled.

## Electrical verification still required

EXTERNAL_STATUS_LEDS_ENABLED remains 0. STATUS_LED_ACTIVE_LEVEL defaults to 1 solely as a configurable
software setting. SAFETY_IO_COMMISSIONED and V20_RUN_COMMISSIONED remain 0. Do not enable these flags
until the corresponding installation has been verified independently.

The vendor documents EXIO1..8 output numbering, GPIO4..11 isolated inputs and optocoupler/Darlington
sinking outputs. The current official Resources page does not provide a board schematic. Therefore this
integration does not claim schematic-level polarity confirmation for the purchased revision. Keep the new
external lamps disabled until the real schematic/revision or manufacturer confirmation and measurements
establish inactive/active behavior, including power-up/reset and failed-I2C cases. Inability to guarantee K2's
inactive image blocks motor commissioning; do not bypass existing gates to test lamps with motor power.

Follow Waveshare documentation for physical output circuits, common terminals, external lamp supply,
load voltage/current, isolation and lamp wiring. Verify K2 DO1 remains inactive during startup and all
fault tests with motor energy independently isolated. No installation was flashed or physically tested.

Primary references checked 2026-10-30:

- [Waveshare board pinout and output type](https://docs.waveshare.com/ESP32-S3-POE-ETH-8DI-8DO)
- [Waveshare official resources](https://docs.waveshare.com/ESP32-S3-POE-ETH-8DI-8DO/Resources-And-Documents)

## Software validation commands

From a short local checkout without spaces (ESP-IDF restriction):

```text
cd ESP32
pio run -e waveshare_s3
pio run -e waveshare_s3_sim
python tests/test_io_configuration.py --database .pio/build/waveshare_s3/compile_commands.json --output .pio/io-config-tests
powershell -NoProfile -ExecutionPolicy Bypass -File tests/run_host_tests.ps1
```

The host suite uses Visual Studio C++ tools and includes real Windows threads for the output
concurrency tests. Execute HMI tests with its requirements installed using
`python -m unittest discover -s tests -v` from HMI. See IO-VALIDATION.md for actual results and
any execution-policy limitations; these checks do not constitute physical commissioning.
