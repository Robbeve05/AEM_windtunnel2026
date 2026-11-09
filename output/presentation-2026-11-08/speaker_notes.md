# Wind Tunnel Project - speaker notes

Language: English. Planned speaking time: **575 seconds (9 minutes 35 seconds)**. Reserve 25 seconds for transitions; questions follow the presentation.

## 1. Title - 20 seconds

Introduce the four-person project and the existing Avans educational wind tunnel. This update concentrates on the new cabinet, the embedded/HMI implementation, and an offline comparison of the two control methods. State that software evidence and installation commissioning are different milestones.

## 2. Objectives and envelope - 35 seconds

Explain the intended student workflow: choose an operating point, acquire pressure data, calculate Cp versus chord location and compare with XFOIL. The 40 m/s value is the configured setpoint ceiling, not a demonstrated maximum flow velocity. Commands are limited to 50 Hz. The ESP32 control clock is 100 Hz; averaged Pitot reports are approximately 20 Hz. The converter's 5.5 kW system rating is not evidence of the motor's rated shaft power.

Sources: `README.md`; `ESP32/include/AppConfig.h`; `ESP32/src/SensorTask.c`; server `Hand-ins/Workplan_windtunnel.pdf`; task system specification.

## 3. Mechanical integration - 35 seconds

Point out the existing open-return structure and the functional location of conditioning, model/taps, diffuser and fan. The diagram is editable but schematic: no dimensional or as-built CAD claim is made. The September workplan identified leaks, a loose door and missing honeycomb. Their current resolution was not verified in this task; describe them as reinspection priorities. Pressure tubing, geometry and reference pressure matter as much as electronics for Cp quality.

Source: server workplan dated 14 September 2026, pages 1-3. No newer mechanical survey or dimensional drawing was found in the inspected project folders.

## 4. Cabinet and architecture - 55 seconds

Use the supplied photo to show tangible assembly progress. The Raspberry Pi, industrial ESP32-S3 and Siemens converter are visible; do not infer wiring correctness from the photograph. Follow the diagram: native HMI and FastAPI, Ethernet to ESP32, asynchronous Modbus RTU to V20, and I2C sensors. Sensor/control notifications alternate at 5 ms while each loop runs at 10 ms. The separate physical safety path must still function when software stops.

Sources: user-supplied new-cabinet photograph; `ESP32/src/ApplicationTasks.c`, `ControlTask.c`, `V20Modbus.c`; `ESP32/include/MachinePins.h`. Photo credit: project owner; supplied in this chat. Visible assembly does not verify protection, earthing or safety integrity.

## 5. HMI - 55 seconds

Explain the dashboard, flow stability and recording controls using the left image. Point to the actual AUTO/MANUAL widget on the right. The source widgets are the current native PySide6 application, not design mockups. All displayed telemetry is locally simulated and no network or hardware command was sent for capture. Applying a setpoint cannot independently start the real tunnel; START and RESET remain physical operations. Mention the source/quality distinction between demonstration data and real acquisition.

Sources: `HMI/desktop.py`, `HMI/main.py`, `HMI/backend/simulator.py`; reproducible `reproduction/capture_hmi.py`. The dashboard history is a deterministic preview trace supplied to the actual chart widget; it is not a recorded experiment. The analytics screenshot is generated from the actual synthetic acquisition/analysis path.

## 6. V20 startup and safety - 60 seconds

The key improvement is separating preparation and starting from running. After physical RESET and healthy inputs, establish OFF1 and release the intended operational OFF2 relay path. Confirm stopped feedback before READY. A new physical START enters STARTING; it sends RUN at zero setpoint, then waits for a fresh enabled status before RUNNING. Explain OFF1 as normal ramp stop; OFF2 as coast/pulse-removal permission; OFF3 is held inactive during ordinary control. STOP and faults pre-empt startup. A software fault lock persists after communication returns; safe OFF1 attempts remain bounded. The diagram simplifies fault arrows: applicable safety/communication faults can interrupt all relevant phases. Physical RESET returns FAULT to stopped recovery/WAIT, not automatically to motor operation.

Sources: committed software `b130348`, `StateMachine.c`, `ControlTask.c`, `V20Modbus.c`, `SafetyIO.c`, `docs/V20_STARTUP_AUDIT.md` section 16. The owner confirmed the intended In1=OFF2 function. As-built wiring/BICO mapping, watchdog action and independent safety behavior remain unverified. Fault acknowledgment is implemented but `V20_FAULT_ACK_VERIFIED=0`.

## 7. Control methods - 55 seconds

Both methods regulate airspeed but have different internal output units. Frequency PI directly adds a frequency correction to frequency feedforward. The experimental torque method produces a torque correction plus inertia/load feedforward, then uses a slip-model inverse to generate Hz. The V20 still receives a frequency reference, never a direct torque command. Frequency PI is selected in the current firmware; it requires fewer uncertain mechanical/electrical parameters. The torque path potentially compensates acceleration/load explicitly, at the cost of parameter sensitivity and more model complexity. Both shown controllers use PI: the stored derivative parameters in the torque design are inactive in PI mode.

Sources: `Regelaar.c`, `TorqueModel.c`, `FrequencyModel.c`, `AppConfig.h`; supplied Python script `ISAController`, `torque_design` and `simulate`. ISA integral uses Tustin; filtered derivative-on-measurement exists but is inactive here. Physical torque-model actuation remains disabled.

## 8. Airspeed comparison - 65 seconds

These traces are regenerated from the owner's supplied `windtunnel_dual_controller_sim.py` with unchanged defaults. Both runs start at rest and use the same nonlinear motor/fan plant, 100 Hz sampling, 1 Hz pressure filter, 0-50 Hz saturation and assumed 2 Hz/s actuator ramp. They follow the same 0/20/30/15 m/s quintic profiles. The shaded period begins at 70 seconds: a shared 1 m/s sinusoidal measured-output disturbance at 0.02 Hz. The two early transitions track reasonably; the torque result diverges substantially during the later scenario. This is a finding to investigate, not evidence that hardware will behave this way.

Configuration: Jmotor=0.0182, Jfan=0.3530, Jtotal=0.3712 kg m^2. Motor assumptions: 4 kW, 1450 rpm, two pole pairs; Kv/Ks/load coefficient follow the supplied formulas. Frequency PI Kp=0.75 Hz/(m/s), Ti=2.142857 s are provisional legacy values. Torque PI Kp=1.7707386 Nm/(m/s), Ti=7.957747 s follows the existing design equations. Both objectives are common tracking/disturbance assessment; the source gains are not matched-bandwidth or jointly optimized. No architecture-only ranking follows.

Differences from firmware: the supplied calculation uses a 15 s first reference profile (the firmware derivative limits may extend it), updates its pressure filter at 100 Hz, and does not include the firmware's five-sample/20 Hz sensor publication pipeline, network timing or V20 startup states. The deployed frequency identification gain/time constant remain zero. These curves are not the commissioned firmware response.

## 9. Output, error and metrics - 65 seconds

Red is frequency PI; navy is torque-model PI. Dotted frequency traces represent the common ramp-limited actuator. The commands remain within 0-50 Hz. Frequency PI uses at most 37.26 Hz; torque at most 43.44 Hz in this run. First-transition 10-90% rise is 9.88 versus 9.97 s, while settling within 2% after the first profile starts is 14.66 versus 30.59 s. First-transition overshoot is 1.29% versus 6.22%. Explain the displayed MAE windows: 25-38 s targets 20 m/s; 99-114 s targets 15 m/s under the continuing disturbance. Late torque MAE is 7.15 m/s versus 0.22 m/s for frequency PI. This combines a down-transition and disturbance, so it is not an isolated disturbance-recovery experiment. The torque inverse's Pitot-based rotor estimate, slow provisional integral and different feedforward need investigation. Do not claim a universal winner or hide the unfavorable result.

Source: exact simulated traces in `plots/original/simulation.csv`; computed metrics in `plots/metrics.json`. Frequency/torque all-run IAE against shaped reference: 40.27/331.00 m. Values are computed, not measured. Optional linear plant unit-input plots in the supplied script use different input units and are not used for ranking controllers.

## 10. Aerodynamic analysis - 50 seconds

Explain Cp normalization and why each tap requires a valid pressure reference and dynamic pressure. The screenshot is the actual analytics widget using the repository's synthetic diamond geometry at five degrees and 14 simulated taps. Upper/lower surface distributions can be compared and integrated with geometry/quality checks. Pressure-derived sectional lift/moment may be available; total drag is not recovered from pressure alone. The physical acquisition adapter is still unconfigured, so no measured Cl or airfoil validation is claimed.

Sources: `HMI/backend/aerodynamics/{acquisition,models,pressure,integration,analysis,quality}.py`, `native_aerodynamics.py`; generated `assets/simulated_aerodynamics.json`. XFOIL is a planned physical comparison, not the origin of this synthetic curve.

## 11. Validation and progress - 40 seconds

Report the recorded passing results: 23 host executables, three firmware builds, 104 HMI tests, seven I/O configuration checks and seven migration tests. Mention that the source-layout test also passes. Highlight the kinds of failure scenarios tested: fresh physical actions, inhibited startup, communication faults, finite STOP attempts, delayed responses and unexpected reset/drive-enable loss. These are software/mock results from commit b130348, not a hardware acceptance certificate.

Sources: `docs/V20_STARTUP_AUDIT.md` section 16 and recorded `output/v20-final-{host,build,hmi,io,layout}.log`, `output/v20-migration.log`. No production source, commissioning flag or V20 parameter was changed while creating this deck. The supplied Python simulation is an additional offline calculation, not one of the 23 firmware regression executables.

## 12. Conclusions and next steps - 40 seconds

Close with the practical order: verify the cabinet/independent safety and V20 settings with motor energy isolated; identify real dynamics and fan calibration; commission controlled starts/stops; validate sensors/flow; finally acquire repeatable pressure distributions. The software framework and cabinet are substantial progress, while the project still needs physical commissioning and experimental validation. A more detailed model is useful only when its parameters and behavior are demonstrated.

Sources: current audit installation-check list, workplan and current model/calibration guards. Permission flags enabled for the requested motor-disconnected bench setup do not supply missing numeric model data or establish physical safety.
