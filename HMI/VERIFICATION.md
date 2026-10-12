# Delivery verification · 2026-10-02

- Python unit/integration tests: 3 passed (`python -m unittest discover -s tests -v`).
- All three JavaScript modules pass `node --check`.
- Browser: no JavaScript error messages during the final workflow.
- All six navigation pages, collapse, setpoint apply, tunnel start/stop, recording start/stop, history refresh and run selection exercised.
- Recorded `UI_Verification_Run01`: 427 samples, about 23 seconds. All four output files inspected on disk. Retained as clearly named simulated verification data.
- Live upper/lower Cp curves rendered; stored curve displayed and recalculated. Export selected, export all, simulated eject and reconnect exercised.
- Stopped and restarted Uvicorn: browser displayed stale telemetry and disabled controls, then reconnected without a page reload.
- Desktop layout measured at 1920×1080: document 1920×1080. Mobile layout checked at 390×844 with no horizontal page overflow.
- Screenshot: `data/hmi-preview.png`.

Physical Raspberry Pi, ESP32, VFD, sensors and USB unmount were not tested. The default USB target is a local simulation directory. Physical USB unmount is delegated to the operating system and documented in README.md.
