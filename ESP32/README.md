# ESP32 firmware

ESP32-specific firmware and hardware project files belong in this directory.

The current pitot sensor firmware is for an **Arduino Uno** and is kept separately at
[`HMI/Arduino/`](../HMI/Arduino/). The host-side Python serial adapter for a future
ESP32 integration remains in [`HMI/backend/esp32.py`](../HMI/backend/esp32.py); it is
not firmware that runs on the ESP32.

No commissioned ESP32 firmware is present yet. Keep the HMI in simulation mode until
the firmware protocol, safety behavior and physical hardware have been validated.
