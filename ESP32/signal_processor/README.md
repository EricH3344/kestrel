# Kestrel Firmware

This repository is pre-bundled with all necessary external software libraries. No additional library searching or manual installation through the Arduino IDE Library Manager is required.

## Sketch structure

The firmware sketch is now split into focused modules so each concern is easier to maintain:

- a_config.ino: pins, globals, PWM capture, logging, and shared helpers
- b_radio_transmission.ino: SBUS packet generation and transmit task
- c_wifi_config.ino: Wi-Fi/AP networking, WebSocket handling, and telemetry broadcast
- d_image_capture.ino: image capture request logic and capture task
- e_dashboard.ino: embedded dashboard HTML and browser-side UI logic
- test.ino: minimal setup/loop entrypoint

## Board Setup Requirements

1. **Board Manager URL**: Open Arduino IDE -> File -> Preferences. Add the following link to **Additional Boards Manager URLs**:
   [https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json](https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json)

2. **Board Manager**: Download esp32 by Espressif Systems, version 2.0.17