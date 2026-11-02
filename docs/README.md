# Documentation guide

This folder is the **entry point** for project documentation. Module-specific technical references remain with their source code under `ESP32/` and `HMI/`. This avoids moving files and accidentally breaking relative links, scripts or references.

## Current documentation

| Reference | Scope |
| --- | --- |
| [Project overview](../README.md) | Overall system, repository navigation and quick start |
| [ESP32 README](../ESP32/README.md) | Current firmware tasks, safety, control, build and commissioning checklist |
| [HMI README](../HMI/README.md) | Native PySide6 interface, FastAPI backend, Windows/Pi installation |
| [Torque control](../ESP32/TORQUE-CONTROL.md) | One-mass torque controller, assumptions and validation still required |
| [V20 Modbus](../ESP32/V20_MODBUS.md) | Siemens V20 integration and drive-related detail |

## Dated or legacy references

| Reference | Status / precautions |
| --- | --- |
| [ESP32 migration](../ESP32/MIGRATION.md) | Historical migration record. **Old DI mapping is not authoritative**; consult current electrical drawing and pin definitions |
| [Pi installation record](../HMI/deploy/PI-INSTALLATION.md) | Dated installation notes; verify the actual installed Pi before using |
| [HMI verification record](../HMI/VERIFICATION.md) | Tests of an earlier browser HMI, not validation of the current PySide6 UI |
| [HMI visual redesign](../HMI/AVANS-REDESIGN.md) | Earlier design history, not current operator documentation |
| [Arduino USB prototype](../HMI/ARDUINO-USB-SETUP.md) | Superseded by the ESP32/Ethernet architecture; **do not use for current hardware** |
| [Dutch Arduino installation guide](../HMI/HANDLEIDING-NEDERLANDS.md) | Historical Arduino Uno installation procedure; **not current deployment** |

## Document maintenance

Update the module README when a module changes. Keep design details close to the module, and update the root README only when system-wide interfaces, capabilities or commissioning status change. Historical records should be visibly labeled rather than silently removed.

**Source of truth:** executable firmware and measured hardware behavior, reviewed electrical drawings and commissioning records, not an older setup guide. Safety-related behavior must be confirmed with a physical test plan.
