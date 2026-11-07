# Documentation index

The root README provides the project overview. The ESP32 and HMI folders each contain a module README. Supporting engineering documents are listed here.

## Current references

- [Project overview](../README.md)
- [ESP32 firmware](../ESP32/README.md)
- [Native HMI](../HMI/README.md)
- [ISA controller and torque/frequency plants](control/TORQUE-CONTROL.md)
- [Refactor validation](control/REFACTOR-VALIDATION.md)
- [Siemens V20 Modbus](hardware/V20_MODBUS.md)
- [Machine I/O](hardware/MACHINE-IO.md) — new schematic mapping; verify against actual firmware and hardware
- [OTA setup](deployment/OTA-SETUP.md) — maintenance-gated, not commissioned

## Historical records

- [Firmware integration summary](archive/INTEGRATION-FINAL.md)
- [Integration validation, 30 October 2026](archive/INTEGRATION-VALIDATION.md)
- [IO validation, 30 October 2026](archive/IO-VALIDATION.md)

- [Previous HMI verification](archive/HMI-VERIFICATION-2026-10-02.md) — earlier browser-based software; not current acceptance evidence
- [Historical HMI design notes](archive/AVANS-REDESIGN.md)
- [Raspberry Pi installation record](../HMI/deploy/PI-INSTALLATION.md) — dated installation details
- [Earlier Arduino Uno guide](../HMI/HANDLEIDING-NEDERLANDS.md) — obsolete hardware instructions

The old ESP32 migration document was removed from the current tree. Its history remains in Git.

Keep active module documentation current, and clearly distinguish software simulation from physical commissioning.
