# Torque-only AUTO controller (R.E. van Eekelen)

AUTO uses exactly one speed loop: airspeed setpoint -> fifth-order reference -> PID torque correction [Nm] + J*d(omega_ref)/dt + fan load -> quasi-static induction-motor inverse -> frequency reference [Hz] -> V20 Modbus -> fan -> Pitot airspeed feedback. MANUAL remains separate and directly commands Hz through its own quintic ramp.

The frequency-domain PI and its preliminary 0.6 s first-order tuning have been removed from the firmware. The PID command from the Pi (PID Kp Ki Kd) now adjusts **torque** gains; these gains must not be confused with prior %/(m/s) gains. Controller coefficients are calculated from RegelaarDesign.c using an approximate one-mass torque-input plant.

The motor inverse uses T_motor=Ks*(2*pi*f/p-omega) and T_fan=b*omega*abs(omega). It is a **local approximation**, not a verified Siemens V20 torque-control characteristic. The torque controller and motor converter both account for acceleration and fan load; the converter receives only the PID torque correction so these terms are NOT added twice. The estimated shaft speed comes from measured airspeed divided by nominal K_v; airflow dynamics/slip can invalidate the estimate during transients.

**Physical AUTO is locked by default.** MOTOR_MODEL_TORQUE_TO_V20_ENABLED=0; both J_MOTOR_KGM2 and J_FAN_KGM2 are zero, so no usable design exists. The 4 kW, 1450 rpm motor parameters in AppConfig.h are placeholders until verified for the installed motor. SAFETY_IO_COMMISSIONED and V20_RUN_COMMISSIONED remain 0. Never enable these flags without physical machine checks. SIMULATION_MODE bypasses the physical AUTO gate only after a valid model is configured.

Check the actual motor nameplate, J_motor, J_fan, V20 V/f setup, slip/torque slope, fan aerodynamic load and Kv. Validate the frequency limits, ramps, steady-state v(f) and transients in simulation and with separate guarded manual experiments before permitting AUTO. A clamped command flags saturation, and the controller rejects integral increments that worsen saturation. Actual V20 ramp tracking can cause additional windup; improve tracking anti-windup before production.

The 200 Hz task, state machine, physical start/reset, STOP/OFF1, watchdog, hardware safety wiring and manual Hz profile are not changed. The hardware safety circuit remains independent of software.
