

/*
 * ESP32 Radar Security System - Configuration Header
 * Professional mmWave Radar with Web Dashboard
 * 
 * Author: Akshay Rathore
 * Version: 2.0.0
 */

#ifndef CONFIG_H
#define CONFIG_H

// ============================================================================
// VERSION & BUILD INFO
// ============================================================================
#define FIRMWARE_VERSION "2.0.0"
#define FIRMWARE_DATE    "2025-01-20"
#define DEVICE_NAME      "ESP32-Radar-Pro"

// ============================================================================
// HARDWARE PINS
// ============================================================================
#define PIN_RADAR_RX     16
#define PIN_RADAR_TX     17
#define PIN_BUZZER       26

// ============================================================================
// RADAR CONFIGURATION - RD-03D by AI-Thinker
// ============================================================================
#define RADAR_BAUD_RATE       115200
#define RADAR_FRAME_SIZE      32
// RD-03D uses F8 header (detected from user's data)
#define RADAR_HEADER_BYTE_1   0xF8
#define RADAR_HEADER_BYTE_2   0x40
#define RADAR_HEADER_BYTE_3   0x83  // Variable - will check in parser
#define RADAR_HEADER_BYTE_4   0x04  // Variable - will check in parser
#define RADAR_MAX_RANGE_MM    8000
#define RADAR_MIN_RANGE_MM    500

// ============================================================================
// SYSTEM LIMITS
// ============================================================================
#define MAX_ZONES             3
#define MAX_TARGETS           5
#define HISTORY_SIZE          100
#define MAX_SSID_LENGTH       32
#define MAX_PASS_LENGTH       64

// ============================================================================
// DEFAULT TIMING VALUES
// ============================================================================
#define DEFAULT_ALARM_DURATION_MS     10000
#define DEFAULT_DETECTION_DISTANCE_MM 8000  // RD-03D supports up to 8m - use full range
#define DEFAULT_TARGET_TIMEOUT_MS     3000
#define DEFAULT_MATCH_DISTANCE_MM     500
#define DEFAULT_MATCH_ANGLE_DEG       25
#define DEFAULT_NOTIFICATION_COOLDOWN 5
#define DEFAULT_SENSITIVITY           80
#define DEFAULT_SWEEP_SPEED           2

// ============================================================================
// NOTIFICATION TYPES
// ============================================================================
enum NotificationType {
    NOTIFY_NONE = 0,
    NOTIFY_TELEGRAM = 1,
    NOTIFY_DISCORD = 2,
    NOTIFY_EMAIL = 3,
    NOTIFY_WEBHOOK = 4,
    NOTIFY_BUZZER_ONLY = 5
};

// ============================================================================
// BUZZER PATTERNS
// ============================================================================
enum BuzzerPattern {
    PATTERN_CONTINUOUS = 0,
    PATTERN_PULSE = 1,
    PATTERN_SOS = 2,
    PATTERN_BEEP = 3,
    PATTERN_WARBLE = 4
};

// ============================================================================
// WIFI CREDENTIALS (Change these for your network)
// ============================================================================
const char* const WIFI_SSID = "YOUR WIFI NAME";
const char* const WIFI_PASSWORD = "YOUR WIFI PASS";

// ============================================================================
// TELEGRAM CONFIGURATION (Pre-configured)
// ============================================================================
#define TELEGRAM_BOT_TOKEN    "YOUR TELEGRAM BOT TOKEN ID"
#define TELEGRAM_CHAT_ID      "YOUR GENERATED CHAT ID"

// ============================================================================
// WEB SERVER CONFIGURATION
// ============================================================================
#define WEB_SERVER_PORT       80
#define JSON_BUFFER_SIZE      2048
#define API_UPDATE_INTERVAL_MS 200

// ============================================================================
// DEBUG CONFIGURATION
// ============================================================================
#define DEBUG_ENABLED         true
#define DEBUG_SERIAL_BAUD     115200

// Macro for debug output
#if DEBUG_ENABLED
    #define DEBUG_PRINT(x)    Serial.print(x)
    #define DEBUG_PRINTLN(x)  Serial.println(x)
    #define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
    #define DEBUG_PRINT(x)
    #define DEBUG_PRINTLN(x)
    #define DEBUG_PRINTF(...)
#endif

#endif // CONFIG_H
