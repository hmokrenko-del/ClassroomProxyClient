#pragma once

// Copy to include/classroom_config.h and set your values.

#define WIFI_SSID "Wokwi-GUEST"
#define WIFI_PASSWORD ""

// For Wokwi + local proxy:
//   host = "host.wokwi.internal", port = 8080
// For shared classroom proxy:
//   host = teacher proxy IP/domain
#define GEMINI_PROXY_HOST "host.wokwi.internal"
#define GEMINI_PROXY_PORT 8080
#define GEMINI_PROXY_PATH "/gemini"

#define GEMINI_MODEL "gemini-2.5-flash"
#define GEMINI_CLIENT_NAME "student-01"

// Sensor pins (adjust for your diagram)
#define PIN_NTC 34
#define PIN_LIGHT 35

// Telemetry intervals (milliseconds)
#define SAMPLE_INTERVAL_MS 500UL
#define LOG_INTERVAL_MS 10000UL
