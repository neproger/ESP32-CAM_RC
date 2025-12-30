# ESP32 firmware (ESP-IDF)

This folder is a self-contained ESP-IDF firmware project for the ESP32‑CAM RC car.

## What it does (matches the Android app + mock server)

- WebSocket server on port `8888`
- Sends camera frames as **binary JPEG** WebSocket messages (ESP32‑CAM)
- Receives 5 control bytes as **binary** WebSocket messages: `[UP, DOWN, LEFT, RIGHT, STOP]`

## Build / flash

Prereqs: ESP-IDF installed and activated in your terminal (or use the VS Code ESP-IDF extension).

From repo root:

```bash
cd ESP32
idf.py set-target esp32
idf.py build
idf.py -p COMx flash monitor
```

## Wi‑Fi modes

- Firmware first tries saved Wi‑Fi credentials from NVS.
- If no credentials are saved (or STA connect fails), firmware starts a SoftAP: `ESP32-CAM-RC` (IP usually `192.168.4.1`).
- You can also hardcode `WIFI_SSID`/`WIFI_PASS` in `ESP32/main/rc_config.h` (not recommended to commit).

## Provisioning (setup portal)

When SoftAP is running:

- Connect your phone/PC to Wi‑Fi network `ESP32-CAM-RC`
- Open `http://192.168.4.1/`
- Pick your router SSID, enter password, press "Save and reboot"

To forget credentials, open the same page and press "Forget saved" (reboots).

## mDNS (device discovery)

Firmware starts mDNS so you can reach it without typing an IP:

- Hostname: `esp32-cam-rc.local` (configurable in `ESP32/main/rc_config.h`)
- WS service discovery (Bonjour/NSD): `_esp_rc._tcp` on port `8888` (Android app uses this)
- Also advertised: `_rcws._tcp` on port `8888` (compat/alternate name)

Note: resolving `*.local` directly from Android varies by version/vendor; the most reliable approach is
to discover `_rcws._tcp` via Android `NsdManager` and use the resolved IP/port.

## Dependencies

Camera support is pulled via ESP-IDF Component Manager:

- `ESP32/main/idf_component.yml`
