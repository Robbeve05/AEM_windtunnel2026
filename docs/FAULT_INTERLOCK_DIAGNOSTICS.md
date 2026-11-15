# Interlock first-cause diagnostics

This change adds evidence only. Fault ordering, boolean safety predicates, timeouts,
ISR latching, DO1 authorization, output transaction counts, and reset transitions
remain unchanged.

## State-machine attribution

`interlock_reason` is a bitmask captured before `StateMachine_Fault`:

| Bit | Name | Existing interlock decision |
| --- | --- | --- |
| 0 | SAFETY_SNAPSHOT_INVALID | Invalid safety snapshot outside INIT |
| 1 | SAFETY_SNAPSHOT_STALE | Valid safety snapshot with age >= INPUT_TIMEOUT_MS outside INIT |
| 2 | DIGITAL_OUTPUT_UNHEALTHY | outputs_ok false outside INIT |
| 3 | SAFETY_NOT_COMMISSIONED | Combined safety check fails in READY/STARTING/RUNNING/STOP/STOPPED_PREPARE after earlier fault checks |
| 4 | SAFETY_RELAY_UNHEALTHY | Reserved; never attributed as an interlock by current branches |
| 5 | VFD_FEEDBACK_UNHEALTHY | Reserved; never attributed as an interlock by current branches |
| 6 | CONTACTOR_FEEDBACK_UNHEALTHY | Reserved; never attributed as an interlock by current branches |

E-stop returns with EMERGENCY_STOP first. Fresh failed relay feedback faults with
SAFETY_FEEDBACK before the interlock branch. VFD and contactor inputs retain VFD
and SAFETY_FEEDBACK respectively. These can coexist with stale/invalid/output
conditions, but must not be mislabeled as the interlock cause. STOP is captured
as evidence and remains an existing stop action, not a new interlock predicate.
Multiple predicates within the reached branch set multiple bits. Invalid and stale
are mutually exclusive classifications; freshness uses the original wrap-safe
millisecond subtraction, including its treatment of future timestamps.

## Immutable first cause

`FirstFaultSnapshot` retains all original fields and adds `triggering_inputs`,
`interlock_reason`, `interlock_decision_us`, safety age, the last output event,
the retained unhealthy output event and the last GPIO event. `triggering_inputs`
is the exact struct passed to Step, copied at the decision before state mutation.
It includes the monotonic microsecond input/decision timestamp, raw DI image,
safety timestamp/validity/commissioning/relay/STOP/E-stop, output health, DO1
request/ack/ack validity, last acknowledgement timestamp, motor Hz and V20 status.
Safety age is the actual millisecond age used in the decision. GPIO/DO diagnostic
copies are taken while assembling these inputs, without subsequent refresh.

The legacy safety and DO1 fields also use the decision copy for an interlock.
Evidence stays immutable through repeated failures, healthy producer refreshes,
and later emergency fault priority. Normal recovery ends the episode; archived
FirstFault evidence remains available until a new episode replaces it.

`do1_shutdown_requested_us` inside FirstFault is the last request already known
at capture (zero if absent). A shutdown caused by the fault necessarily occurs
later. Its first observed request timestamp is published separately as
`fault_do1_shutdown_requested_us` / `FirstFaultPost shutdown_requested_us`, without
mutating FirstFault. It records software intent, before the I2C transaction, and
is distinct from the existing DO1/OFF2 observation timestamps. No timestamp
claims physical relay movement or physical edge ordering.

## Output events

Event reasons: 0 none, 1 I2C failure, 2 latch readback mismatch, 3 missing software
acknowledgement (no initialized writer/lock), 4 software DO1 shutdown.
Operations: 0 none, 1 output-register write, 2 output-latch read, 3 direction write.
An event contains validity, timestamp, sequence, address, operation, driver error,
bus result, requested image, last successful write image and validity, readback
image and validity, wire transaction duration, and logical DO1 request/ack.
Wire duration excludes mutex wait, matching I2CBus diagnostics. Busy has no driver
error and is identified by its bus result. Per-call results are returned directly
by I2CBus; diagnostics never infer a cause from later shared counters.

Health transitions emit one event; sustained unhealthy polling does not overwrite
the first edge. The last unhealthy event survives a later software shutdown.
Initialization transaction failures are also retained. No additional I2C reads,
retries, acknowledgement grace period or safety freshness threshold is added.
Acknowledgement validity and last-ACK time permit identification of missing/old
acknowledgements. There is no separate stale-output-ACK timeout in the existing
logic: a failed current probe is unhealthy immediately. Stale safety snapshots
are attributed separately, rather than inventing a new output timeout.

Output-register readback and successful I2C ACKs acknowledge software/register
images only. They never establish physical relay, contactor or motor state.
`OutputEvent` logs are emitted on a new sequence from the diagnostic task; the
FirstFault copies retain the last sampled events if periodic logging misses them.

## GPIO events

The existing ISR latch checks run before diagnostic storage. Each interrupt
records the source GPIO, raw levels and configured polarities for E-stop/relay/
STOP, latch state, microsecond timestamp, event generation and existing unsafe
generation. A preallocated 16-entry overwrite ring is protected by the existing
critical section. Task-context `SafetyIO_ReadEvents` drains oldest-first; an
explicit cumulative overwrite counter reports loss. `SafetyIO_GetLastEvent`
remains available after draining. The diagnostic task publishes `SafetyGPIO`
records. There is no allocation, printing, queue wait or debounce in the ISR.

## Verification

Host tests cover attribution at the timeout boundary, immutable exact inputs,
healthy producer refresh before publication, dedicated relay fault priority,
output write/read errors and latch mismatch, polarity and concurrent output
writers, GPIO timestamp/generation/latch capture and bounded ring overflow.
Firmware compilation verifies the real ESP-IDF interrupt and timer APIs.
Hardware wiring, interrupt latency and physical relay behavior require bench
measurement; host tests do not establish those properties.
