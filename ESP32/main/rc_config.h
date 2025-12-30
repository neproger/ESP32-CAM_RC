#pragma once

// === Wi-Fi ===
// Set WIFI_SSID/WIFI_PASS for STA mode.
// Leave WIFI_SSID empty to start a SoftAP (AP mode).
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif

#ifndef WIFI_PASS
#define WIFI_PASS ""
#endif

// STA connect behavior (used when SSID is present, or saved in NVS).
#ifndef WIFI_STA_MAX_RETRY
#define WIFI_STA_MAX_RETRY 5
#endif

#ifndef WIFI_STA_CONNECT_TIMEOUT_MS
#define WIFI_STA_CONNECT_TIMEOUT_MS 15000
#endif

// Debug: log sensitive data (Wi‑Fi password) to serial output.
// Keep this disabled for normal use.
#ifndef PROVISION_LOG_SENSITIVE
#define PROVISION_LOG_SENSITIVE 0
#endif

// === mDNS ===
#ifndef MDNS_INSTANCE
#define MDNS_INSTANCE "ESP32-RC-CAR01"
#endif

// Hostname will be: http://<MDNS_HOSTNAME>.local/
#ifndef MDNS_HOSTNAME
#define MDNS_HOSTNAME MDNS_INSTANCE
#endif



#ifndef AP_SSID
#define AP_SSID MDNS_INSTANCE
#endif

#ifndef AP_PASS
#define AP_PASS ""
#endif

// === Protocol ===
// Android client uses: ws://<ip>:8888   (path defaults to "/")
#ifndef RC_WS_PORT
#define RC_WS_PORT 8888
#endif

// Control bytes: [UP, DOWN, LEFT, RIGHT, STOP]
#define RC_CONTROL_LEN 5

// === Stream ===
#ifndef STREAM_FPS
#define STREAM_FPS 5
#endif

// Camera task behavior
// CAM_INIT_MAX_RETRIES: 1 = single attempt, 0 = retry forever
#ifndef CAM_INIT_MAX_RETRIES
#define CAM_INIT_MAX_RETRIES 1
#endif

#ifndef CAM_INIT_RETRY_DELAY_MS
#define CAM_INIT_RETRY_DELAY_MS 2000
#endif

#ifndef CAM_STREAM_IDLE_DELAY_MS
#define CAM_STREAM_IDLE_DELAY_MS 200
#endif

// === Camera (AI Thinker ESP32-CAM pinout) ===
#ifndef CAM_PIN_PWDN
#define CAM_PIN_PWDN 32
#endif
#ifndef CAM_PIN_RESET
#define CAM_PIN_RESET -1
#endif
#ifndef CAM_PIN_XCLK
#define CAM_PIN_XCLK 0
#endif
#ifndef CAM_PIN_SIOD
#define CAM_PIN_SIOD 26
#endif
#ifndef CAM_PIN_SIOC
#define CAM_PIN_SIOC 27
#endif
#ifndef CAM_PIN_Y9
#define CAM_PIN_Y9 35
#endif
#ifndef CAM_PIN_Y8
#define CAM_PIN_Y8 34
#endif
#ifndef CAM_PIN_Y7
#define CAM_PIN_Y7 39
#endif
#ifndef CAM_PIN_Y6
#define CAM_PIN_Y6 36
#endif
#ifndef CAM_PIN_Y5
#define CAM_PIN_Y5 21
#endif
#ifndef CAM_PIN_Y4
#define CAM_PIN_Y4 19
#endif
#ifndef CAM_PIN_Y3
#define CAM_PIN_Y3 18
#endif
#ifndef CAM_PIN_Y2
#define CAM_PIN_Y2 5
#endif
#ifndef CAM_PIN_VSYNC
#define CAM_PIN_VSYNC 25
#endif
#ifndef CAM_PIN_HREF
#define CAM_PIN_HREF 23
#endif
#ifndef CAM_PIN_PCLK
#define CAM_PIN_PCLK 22
#endif
