/*
 * ESP32 Radar Security System - Professional Edition
 * ==================================================
 * Advanced mmWave radar with real-time tracking, zone-based detection,
 * multi-channel notifications, and modern web dashboard.
 * 
 * Features:
 * - Real-time multi-target tracking with movement trails
 * - 3 configurable security zones
 * - Telegram/Discord/Webhook notifications
 * - 5 buzzer alert patterns
 * - Persistent settings storage
 * - Detection history logging
 * - Modern responsive web UI
 * 
 * Hardware: ESP32 + mmWave Radar Module by aiThinker
 * 
 * Author: Akshay Rathore
 * Version: 2.0.0
 * Date: 2025-01-20
 */

#include "config.h"
#include "types.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <math.h>
#include <Preferences.h>
#include <string.h>

// ============================================================================
// GLOBAL OBJECTS
// ============================================================================

Preferences prefs;
HardwareSerial RadarSerial(2);
WebServer server(WEB_SERVER_PORT);
WiFiClient wifiClient;

// ============================================================================
// SYSTEM STATE
// ============================================================================

SystemConfig config;
NotificationConfig notifyConfig;
Zone zones[MAX_ZONES];
Target targets[MAX_TARGETS];
DetectionEvent history[HISTORY_SIZE];

RadarFrame radarFrame;
uint16_t targetCounter = 0;
uint8_t historyIndex = 0;

// Current radar reading
uint16_t radarDistance = 0;
float radarAngle = 0;
bool targetDetected = false;
int8_t targetZone = -1;

// Alarm state
uint32_t lastBlink = 0;
bool blinkState = false;
uint32_t lastRadarRead = 0;

// ============================================================================
// FUNCTION PROTOTYPES
// ============================================================================

// Setup & Initialization
void initializeSystem();
void initializeWiFi();
void initializeRadar();
void initializeZones();
void initializeTargets();
void initializeNotifications();
void setupWebServer();

// Radar Processing
void processRadarData();
void parseRadarFrame();
int8_t checkZone(uint16_t distance, float angle);
void updateTarget(uint16_t dist, float angle, int16_t speed);
void cleanupTargets();
uint8_t getActiveTargetCount();

// Alarm & Notifications
void handleAlarm();
void updateAlarmBlink();
void updateBuzzer();
bool sendNotification(const char* title, const char* message, bool bypassCooldown = false);
bool sendTelegram(const char* title, const char* message);
bool sendDiscord(const char* title, const char* message);
bool sendWebhook(const char* title, const char* message);

// Data Logging
void logEvent(int8_t zoneId, uint16_t targetId, uint16_t dist, float angle);

// Settings Persistence
void loadSettings();
void saveSettings();
void loadNotificationSettings();
void saveNotificationSettings();

// Web Server Handlers
void handleRoot();
void handleRadarData();
void handleTargets();
void handleZones();
void handleStats();
void handleHistory();
void handleGetSettings();
void handleSetSettings();
void handleGetNotifications();
void handleSetNotifications();
void handleArm();
void handleDisarm();
void handleTestBuzzer();
void handleTestNotification();

// Web UI
String generateWebUI();

// ============================================================================
// SETUP & MAIN LOOP
// ============================================================================

void setup() {
    Serial.begin(DEBUG_SERIAL_BAUD);
    delay(100);
    
    DEBUG_PRINTLN(F("\n========================================"));
    DEBUG_PRINTLN(F("  ESP32 Radar Security System v" FIRMWARE_VERSION));
    DEBUG_PRINTLN(F("  " DEVICE_NAME));
    DEBUG_PRINTLN(F("========================================\n"));
    
    initializeSystem();
    
    DEBUG_PRINTLN(F("[OK] System initialization complete"));
    DEBUG_PRINT(F("[INFO] Device IP: "));
    DEBUG_PRINTLN(WiFi.localIP());
}

void loop() {
    server.handleClient();
    processRadarData();
    handleAlarm();
    updateAlarmBlink();
    updateBuzzer();
    
    // Debug: Print raw bytes if available
    static uint32_t lastDebug = 0;
    if (millis() - lastDebug > 1000) {
        lastDebug = millis();
        if (RadarSerial.available()) {
            DEBUG_PRINT(F("[RADAR] Data available: "));
            DEBUG_PRINTLN(RadarSerial.available());
        }
    }
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void initializeSystem() {
    // Initialize preferences
    prefs.begin("radar", false);
    
    // Load saved settings
    loadSettings();
    loadNotificationSettings();
    
    // Initialize hardware
    initializeWiFi();
    initializeRadar();
    
    // Initialize data structures
    initializeZones();
    initializeTargets();
    initializeNotifications();
    
    // Record startup time
    config.uptimeStart = millis();
    
    // Setup web server
    setupWebServer();
    server.begin();
}

void initializeWiFi() {
    DEBUG_PRINT(F("[WIFI] Connecting to "));
    DEBUG_PRINTLN(WIFI_SSID);
    
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    uint8_t attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        DEBUG_PRINT(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        DEBUG_PRINTLN(F("\n[WIFI] Connected successfully"));
    } else {
        DEBUG_PRINTLN(F("\n[WIFI] Connection failed!"));
    }
}

void initializeRadar() {
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);
    
    RadarSerial.begin(RADAR_BAUD_RATE, SERIAL_8N1, PIN_RADAR_RX, PIN_RADAR_TX);
    DEBUG_PRINT(F("[RADAR] Serial initialized at "));
    DEBUG_PRINT(RADAR_BAUD_RATE);
    DEBUG_PRINTLN(F(" baud"));
}

void initializeZones() {
    // RD-03D zones - full 8m range, all angles
    // Single zone covering all detection area
    zones[0] = Zone(true, 500, 8000, 0, 180, "All");    // Full coverage
    zones[1] = Zone(false, 0, 0, 0, 0, "Unused");       // Disabled
    zones[2] = Zone(false, 0, 0, 0, 0, "Unused");       // Disabled
}

void initializeTargets() {
    for (uint8_t i = 0; i < MAX_TARGETS; i++) {
        targets[i].active = false;
    }
}

void initializeNotifications() {
    notifyConfig.type = NOTIFY_TELEGRAM;
    notifyConfig.notifyOnDetection = false;
    notifyConfig.notifyOnAlarm = true;
    notifyConfig.cooldownMinutes = DEFAULT_NOTIFICATION_COOLDOWN;
    notifyConfig.lastNotification = 0;
    
    strncpy(notifyConfig.telegramBotToken, TELEGRAM_BOT_TOKEN, 63);
    strncpy(notifyConfig.telegramChatId, TELEGRAM_CHAT_ID, 31);
    notifyConfig.telegramBotToken[63] = '\0';
    notifyConfig.telegramChatId[31] = '\0';
}

// ============================================================================
// RADAR PROCESSING
// ============================================================================

void processRadarData() {
    static uint8_t tempBuffer[64];
    static uint8_t tempIndex = 0;
    static uint32_t rawByteCount = 0;
    
    while (RadarSerial.available()) {
        uint8_t byte = RadarSerial.read();
        rawByteCount++;
        
        // Debug: Print first 50 raw bytes to see what we're getting
        static bool showedRaw = false;
        if (!showedRaw && rawByteCount <= 50) {
            DEBUG_PRINTF("%02X ", byte);
            if (rawByteCount == 50) {
                DEBUG_PRINTLN();
                showedRaw = true;
            }
        }
        
        // RD-03D frame header: F8 40 (first 2 bytes fixed, rest variable)
        if (tempIndex == 0 && byte != RADAR_HEADER_BYTE_1) continue;
        if (tempIndex == 1 && byte != RADAR_HEADER_BYTE_2) {
            tempIndex = 0;
            continue;
        }
        // Bytes 2 and 3 are variable, accept any
        if (tempIndex >= 4) {
            // Check for frame end marker FD
            if (byte == 0xFD && tempIndex >= 10) {
                // Found end of frame - store the FD byte first
                if (tempIndex < 64) {
                    tempBuffer[tempIndex] = byte;
                }
                // Copy to radarFrame and parse
                memcpy(radarFrame.buffer, tempBuffer, tempIndex + 1);
                radarFrame.index = tempIndex + 1;
                parseRadarFrame();
                tempIndex = 0;
                showedRaw = false;
                continue;
            }
        }
        
        // Store byte if not already stored by end-of-frame detection
        if (tempIndex < 64) {
            tempBuffer[tempIndex++] = byte;
        }
        
        // Safety: reset if buffer full
        if (tempIndex >= 64) {
            tempIndex = 0;
        }
    }
}

void parseRadarFrame() {
    // RD-03D Protocol - Multi-target tracking with false detection filtering
    
    if (radarFrame.index < 11) {
        return;
    }
    
    uint8_t validTargets = 0;
    uint16_t processedDists[3] = {0, 0, 0}; // Track processed distances
    
    // Parse up to 3 targets from frame
    for (uint8_t t = 0; t < 3; t++) {
        uint8_t offset = 2 + (t * 6);
        if (offset + 5 >= radarFrame.index) break;
        if (radarFrame.buffer[offset] == 0xFD) break;
        
        uint8_t targetId = radarFrame.buffer[offset];
        uint8_t distLow = radarFrame.buffer[offset + 1];
        uint8_t distHigh = radarFrame.buffer[offset + 2];
        uint8_t strength = radarFrame.buffer[offset + 3];
        
        uint16_t distMm = ((uint16_t)distHigh << 8) | distLow;
        
        // === FALSE DETECTION FILTERS ===
        
        // 1. Distance range check (RD-03D min is ~0.3m, max ~8m)
        if (distMm < 300 || distMm > 8000) continue;
        
        // 2. Signal strength check (higher = more reliable)
        if (strength < 5) continue; // Increased threshold
        
        // 3. Ignore static/fixed distance readings (false reflections)
        // Only apply if person is NOT moving (stationary for long time)
        static uint16_t lastDist = 0;
        static uint8_t sameDistCount = 0;
        static uint32_t lastMoveTime = 0;
        
        if (abs((int)distMm - (int)lastDist) < 100) {
            sameDistCount++;
            // Only filter if stuck at SAME spot for 50+ frames AND not recently moved
            if (sameDistCount > 50 && (millis() - lastMoveTime > 10000)) {
                // This might be a static reflection, but still allow it
                // Just reduce confidence - don't skip entirely
            }
        } else {
            // Movement detected!
            sameDistCount = 0;
            lastMoveTime = millis();
        }
        lastDist = distMm;
        
        // 4. Check against already processed targets (avoid duplicates)
        bool isDuplicate = false;
        for (uint8_t v = 0; v < validTargets; v++) {
            if (abs((int)distMm - (int)processedDists[v]) < 800) {
                isDuplicate = true;
                break;
            }
        }
        if (isDuplicate) continue;
        
        // 5. Validate target ID (some IDs are noise)
        // Valid IDs typically: 0x00-0x7F or 0x80-0xBF
        if (targetId > 0xC0 && strength < 10) continue;
        
        // === PROCESS VALID TARGET ===
        
        // Calculate angle from ID
        float angle = 90.0f;
        uint8_t idLower = targetId & 0x7F;
        
        if (targetId >= 0x80) {
            angle = 90.0f + ((targetId - 0x80) * 1.4f);
        } else {
            angle = idLower * 1.4f;
        }
        if (angle > 180) angle = 180;
        if (angle < 0) angle = 0;
        
        // Store for first target display
        if (validTargets == 0) {
            radarDistance = distMm;
            radarAngle = angle;
            targetZone = checkZone(radarDistance, radarAngle);
            targetDetected = (targetZone >= 0);
        }
        
        // Save distance for duplicate check
        processedDists[validTargets] = distMm;
        
        // Update/create target
        updateTarget(distMm, angle, strength);
        validTargets++;
        
        DEBUG_PRINTF("[RADAR] T%d: ID=0x%02X, Dist=%dmm, Angle=%.1f, Str=%d\n", 
                     validTargets, targetId, distMm, angle, strength);
    }
    
    if (validTargets == 0) {
        targetDetected = false;
        targetZone = -1;
    }
    
    cleanupTargets();
    lastRadarRead = millis();
}

int8_t checkZone(uint16_t distance, float angle) {
    for (uint8_t i = 0; i < MAX_ZONES; i++) {
        if (!zones[i].enabled) continue;
        if (distance >= zones[i].minDist && distance <= zones[i].maxDist &&
            angle >= zones[i].minAngle && angle <= zones[i].maxAngle) {
            return i;
        }
    }
    return -1;
}

void updateTarget(uint16_t dist, float angle, int16_t speedVal) {
    int bestIdx = -1;
    uint32_t bestAge = 0xFFFFFFFF;
    
    // Try to match with existing target
    for (uint8_t i = 0; i < MAX_TARGETS; i++) {
        if (targets[i].active) {
            uint16_t dDiff = abs((int)targets[i].distance - (int)dist);
            uint16_t aDiff = abs((int)(targets[i].angle - angle));
            
            if (dDiff < DEFAULT_MATCH_DISTANCE_MM && aDiff < DEFAULT_MATCH_ANGLE_DEG) {
                // Update existing target
                targets[i].prevDistance = targets[i].distance;
                targets[i].prevAngle = targets[i].angle;
                targets[i].distance = dist;
                targets[i].angle = angle;
                targets[i].speed = speedVal;
                targets[i].lastSeen = millis();
                targets[i].rssi = speedVal;
                return;
            }
            
            if (millis() - targets[i].lastSeen < bestAge) {
                bestAge = millis() - targets[i].lastSeen;
                bestIdx = i;
            }
        }
    }
    
    // Create new target
    for (uint8_t i = 0; i < MAX_TARGETS; i++) {
        if (!targets[i].active) {
            targets[i].active = true;
            targets[i].id = ++targetCounter;
            targets[i].distance = dist;
            targets[i].angle = angle;
            targets[i].prevDistance = 0;
            targets[i].prevAngle = 0;
            targets[i].speed = speedVal;
            targets[i].firstSeen = millis();
            targets[i].lastSeen = millis();
            targets[i].rssi = speedVal;
            logEvent(targetZone, targetCounter, dist, angle);
            config.totalDetections++;
            return;
        }
    }
    
    // Overwrite oldest if all slots full
    if (bestIdx >= 0 && millis() - targets[bestIdx].lastSeen > 2000) {
        targets[bestIdx].active = true;
        targets[bestIdx].id = ++targetCounter;
        targets[bestIdx].prevDistance = targets[bestIdx].distance;
        targets[bestIdx].prevAngle = targets[bestIdx].angle;
        targets[bestIdx].distance = dist;
        targets[bestIdx].angle = angle;
        targets[bestIdx].speed = speedVal;
        targets[bestIdx].firstSeen = millis();
        targets[bestIdx].lastSeen = millis();
        targets[bestIdx].rssi = speedVal;
    }
}

void cleanupTargets() {
    for (uint8_t i = 0; i < MAX_TARGETS; i++) {
        if (targets[i].active && millis() - targets[i].lastSeen > 1500) {
            targets[i].active = false;
        }
    }
}

uint8_t getActiveTargetCount() {
    uint8_t count = 0;
    for (uint8_t i = 0; i < MAX_TARGETS; i++) {
        if (targets[i].active) count++;
    }
    return count;
}

// ============================================================================
// ALARM & NOTIFICATIONS
// ============================================================================

void handleAlarm() {
    if (!config.systemEnabled || !config.armed) {
        config.alarmActive = false;
        return;
    }
    
    if (targetDetected && !config.alarmActive) {
        config.alarmActive = true;
        config.alarmStartTime = millis();
        
        if (notifyConfig.notifyOnAlarm) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Target in zone %d at %.2fm", 
                     targetZone, radarDistance / 1000.0f);
            sendNotification("ALARM TRIGGERED", msg);
        }
    }
    
    if (config.alarmActive && millis() - config.alarmStartTime > config.alarmDuration) {
        config.alarmActive = false;
    }
}

void updateAlarmBlink() {
    if (config.alarmActive) {
        if (millis() - lastBlink > 250) {
            blinkState = !blinkState;
            lastBlink = millis();
        }
    } else {
        blinkState = false;
    }
}

void updateBuzzer() {
    if (!config.soundEnabled || !config.alarmActive) {
        digitalWrite(PIN_BUZZER, LOW);
        return;
    }
    
    uint32_t now = millis();
    uint32_t elapsed = now - config.alarmStartTime;
    bool on = false;
    
    switch (config.buzzerPattern) {
        case PATTERN_CONTINUOUS:
            on = true;
            break;
        case PATTERN_PULSE:
            on = blinkState;
            break;
        case PATTERN_SOS: {
            uint8_t phase = (elapsed / 250) % 19;
            on = (phase < 3 || (phase >= 7 && phase < 10) || (phase >= 14 && phase < 17));
            break;
        }
        case PATTERN_BEEP: {
            uint8_t phase = (elapsed / 500) % 2;
            on = (phase == 0);
            break;
        }
        case PATTERN_WARBLE: {
            uint16_t freq = 10 + (elapsed / 50) % 20;
            on = (elapsed % (2 * freq) < freq);
            break;
        }
    }
    
    digitalWrite(PIN_BUZZER, on ? HIGH : LOW);
}

bool sendNotification(const char* title, const char* message, bool bypassCooldown) {
    uint32_t now = millis();
    uint32_t cooldownMs = notifyConfig.cooldownMinutes * 60000;
    
    DEBUG_PRINTLN(F("[NOTIFY] Sending notification..."));
    
    if (notifyConfig.type == NOTIFY_NONE) {
        DEBUG_PRINTLN(F("[NOTIFY] Type is NONE"));
        return false;
    }
    
    if (!bypassCooldown && (now - notifyConfig.lastNotification < cooldownMs)) {
        DEBUG_PRINTLN(F("[NOTIFY] Cooldown active"));
        return false;
    }
    
    bool success = false;
    
    switch (notifyConfig.type) {
        case NOTIFY_TELEGRAM:
            success = sendTelegram(title, message);
            break;
        case NOTIFY_DISCORD:
            success = sendDiscord(title, message);
            break;
        case NOTIFY_WEBHOOK:
            success = sendWebhook(title, message);
            break;
        case NOTIFY_BUZZER_ONLY:
            success = true;
            break;
        default:
            break;
    }
    
    if (success) {
        notifyConfig.lastNotification = now;
    }
    return success;
}

bool sendTelegram(const char* title, const char* message) {
    // Check if configured
    if (strlen(notifyConfig.telegramBotToken) == 0 || 
        strlen(notifyConfig.telegramChatId) == 0) {
        DEBUG_PRINTLN(F("[TELEGRAM] Not configured"));
        return false;
    }
    
    DEBUG_PRINTLN(F("[TELEGRAM] Connecting..."));
    
    WiFiClientSecure client;
    client.setInsecure();
    
    if (!client.connect("api.telegram.org", 443)) {
        DEBUG_PRINTLN(F("[TELEGRAM] Connection failed"));
        return false;
    }
    
    DEBUG_PRINTLN(F("[TELEGRAM] Connected"));
    
    // Build payload
    String payload = "{\"chat_id\":\"";
    payload += notifyConfig.telegramChatId;
    payload += "\",\"text\":\"";
    payload += title;
    payload += ": ";
    payload += message;
    payload += "\"}";
    
    String url = "/bot";
    url += notifyConfig.telegramBotToken;
    url += "/sendMessage";
    
    DEBUG_PRINT(F("[TELEGRAM] URL: "));
    DEBUG_PRINTLN(url);
    
    // Send HTTP request
    client.print(String("POST ") + url + " HTTP/1.1\r\n");
    client.print("Host: api.telegram.org\r\n");
    client.print("Content-Type: application/json\r\n");
    client.print("Content-Length: " + String(payload.length()) + "\r\n");
    client.print("Connection: close\r\n\r\n");
    client.print(payload);
    
    DEBUG_PRINTLN(F("[TELEGRAM] Request sent"));
    
    // Wait for response
    uint32_t timeout = millis();
    while (client.available() == 0) {
        if (millis() - timeout > 10000) {
            DEBUG_PRINTLN(F("[TELEGRAM] Timeout"));
            return false;
        }
        delay(10);
    }
    
    // Read response
    String response = "";
    while (client.available()) {
        response += (char)client.read();
    }
    
    DEBUG_PRINT(F("[TELEGRAM] Response: "));
    DEBUG_PRINTLN(response.substring(0, 100)); // Print first 100 chars
    
    return response.indexOf("\"ok\":true") >= 0;
}

bool sendDiscord(const char* title, const char* message) {
    if (strlen(notifyConfig.discordWebhook) == 0) return false;
    
    String payload = "{\"content\":\"";
    payload += title;
    payload += ": ";
    payload += message;
    payload += "\"}";
    
    WiFiClientSecure client;
    client.setInsecure();
    
    String url = notifyConfig.discordWebhook;
    url.replace("https://", "");
    int slashIndex = url.indexOf('/');
    if (slashIndex <= 0) return false;
    
    String host = url.substring(0, slashIndex);
    String path = url.substring(slashIndex);
    
    if (!client.connect(host.c_str(), 443)) return false;
    
    client.print(String("POST ") + path + " HTTP/1.1\r\n");
    client.print("Host: " + host + "\r\n");
    client.print("Content-Type: application/json\r\n");
    client.print("Content-Length: " + String(payload.length()) + "\r\n");
    client.print("Connection: close\r\n\r\n");
    client.print(payload);
    
    uint32_t timeout = millis();
    while (client.available() == 0) {
        if (millis() - timeout > 5000) return false;
    }
    
    return true;
}

bool sendWebhook(const char* title, const char* message) {
    if (strlen(notifyConfig.webhookUrl) == 0) return false;
    
    String payload = "{\"title\":\"";
    payload += title;
    payload += "\",\"message\":\"";
    payload += message;
    payload += "\",\"timestamp\":";
    payload += String(millis());
    payload += ",\"device\":\"" DEVICE_NAME "\"}";
    
    String url = notifyConfig.webhookUrl;
    bool isHttps = url.startsWith("https");
    url.replace("https://", "");
    url.replace("http://", "");
    
    int slashIndex = url.indexOf('/');
    String host = (slashIndex > 0) ? url.substring(0, slashIndex) : url;
    String path = (slashIndex > 0) ? url.substring(slashIndex) : "/";
    int port = isHttps ? 443 : 80;
    
    if (isHttps) {
        WiFiClientSecure client;
        client.setInsecure();
        if (!client.connect(host.c_str(), port)) return false;
        
        client.print(String("POST ") + path + " HTTP/1.1\r\n");
        client.print("Host: " + host + "\r\n");
        client.print("Content-Type: application/json\r\n");
        client.print("Content-Length: " + String(payload.length()) + "\r\n");
        client.print("Connection: close\r\n\r\n");
        client.print(payload);
    } else {
        if (!wifiClient.connect(host.c_str(), port)) return false;
        
        wifiClient.print(String("POST ") + path + " HTTP/1.1\r\n");
        wifiClient.print("Host: " + host + "\r\n");
        wifiClient.print("Content-Type: application/json\r\n");
        wifiClient.print("Content-Length: " + String(payload.length()) + "\r\n");
        wifiClient.print("Connection: close\r\n\r\n");
        wifiClient.print(payload);
    }
    
    return true;
}

// ============================================================================
// DATA LOGGING
// ============================================================================

void logEvent(int8_t zoneId, uint16_t targetId, uint16_t dist, float angle) {
    history[historyIndex].timestamp = millis();
    history[historyIndex].zoneId = zoneId;
    history[historyIndex].targetId = targetId;
    history[historyIndex].distance = dist;
    history[historyIndex].angle = angle;
    historyIndex = (historyIndex + 1) % HISTORY_SIZE;
}

// ============================================================================
// SETTINGS PERSISTENCE
// ============================================================================

void loadSettings() {
    config.armed = prefs.getBool("armed", true);
    config.detectionDistance = prefs.getInt("dist", DEFAULT_DETECTION_DISTANCE_MM);
    config.alarmDuration = prefs.getInt("duration", DEFAULT_ALARM_DURATION_MS);
    config.sensitivity = prefs.getInt("sens", DEFAULT_SENSITIVITY);
    config.buzzerPattern = prefs.getInt("pattern", PATTERN_CONTINUOUS);
    config.soundEnabled = prefs.getBool("sound", true);
    config.sweepSpeed = prefs.getInt("sweep", DEFAULT_SWEEP_SPEED);
    config.totalDetections = prefs.getULong("total", 0);
}

void saveSettings() {
    prefs.putBool("armed", config.armed);
    prefs.putInt("dist", config.detectionDistance);
    prefs.putInt("duration", config.alarmDuration);
    prefs.putInt("sens", config.sensitivity);
    prefs.putInt("pattern", config.buzzerPattern);
    prefs.putBool("sound", config.soundEnabled);
    prefs.putInt("sweep", config.sweepSpeed);
    prefs.putULong("total", config.totalDetections);
}

void loadNotificationSettings() {
    notifyConfig.type = (NotificationType)prefs.getInt("notifyType", NOTIFY_TELEGRAM);
    notifyConfig.notifyOnDetection = prefs.getBool("notifyDetect", false);
    notifyConfig.notifyOnAlarm = prefs.getBool("notifyAlarm", true);
    notifyConfig.cooldownMinutes = prefs.getInt("notifyCooldown", DEFAULT_NOTIFICATION_COOLDOWN);
    
    String chatId = prefs.getString("tgChatId", TELEGRAM_CHAT_ID);
    String webhook = prefs.getString("webhook", "");
    
    strncpy(notifyConfig.telegramChatId, chatId.c_str(), 31);
    strncpy(notifyConfig.webhookUrl, webhook.c_str(), 127);
    notifyConfig.telegramChatId[31] = '\0';
    notifyConfig.webhookUrl[127] = '\0';
}

void saveNotificationSettings() {
    prefs.putInt("notifyType", notifyConfig.type);
    prefs.putBool("notifyDetect", notifyConfig.notifyOnDetection);
    prefs.putBool("notifyAlarm", notifyConfig.notifyOnAlarm);
    prefs.putInt("notifyCooldown", notifyConfig.cooldownMinutes);
    prefs.putString("tgChatId", notifyConfig.telegramChatId);
    prefs.putString("webhook", notifyConfig.webhookUrl);
}

// ============================================================================
// WEB SERVER SETUP
// ============================================================================

void setupWebServer() {
    server.on("/", HTTP_GET, handleRoot);
    server.on("/api/radar-data", HTTP_GET, handleRadarData);
    server.on("/api/targets", HTTP_GET, handleTargets);
    server.on("/api/zones", HTTP_GET, handleZones);
    server.on("/api/stats", HTTP_GET, handleStats);
    server.on("/api/history", HTTP_GET, handleHistory);
    server.on("/api/settings", HTTP_GET, handleGetSettings);
    server.on("/api/settings", HTTP_POST, handleSetSettings);
    server.on("/api/notifications", HTTP_GET, handleGetNotifications);
    server.on("/api/notifications", HTTP_POST, handleSetNotifications);
    server.on("/api/arm", HTTP_POST, handleArm);
    server.on("/api/disarm", HTTP_POST, handleDisarm);
    server.on("/api/test-buzzer", HTTP_POST, handleTestBuzzer);
    server.on("/api/test-notification", HTTP_POST, handleTestNotification);
    server.enableCORS(true);
}

// ============================================================================
// WEB HANDLERS
// ============================================================================

void handleRoot() {
    server.send(200, "text/html", generateWebUI());
}

void handleRadarData() {
    StaticJsonDocument<512> doc;
    doc["distance"] = radarDistance;
    doc["angle"] = radarAngle;
    doc["detected"] = targetDetected;
    doc["zone"] = targetZone;
    doc["armed"] = config.armed;
    doc["alarmActive"] = config.alarmActive;
    doc["systemEnabled"] = config.systemEnabled;
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void handleTargets() {
    StaticJsonDocument<2048> doc;
    JsonArray arr = doc.createNestedArray("targets");
    
    for (uint8_t i = 0; i < MAX_TARGETS; i++) {
        if (targets[i].active) {
            JsonObject t = arr.createNestedObject();
            t["id"] = targets[i].id;
            t["distance"] = targets[i].distance;
            t["angle"] = targets[i].angle;
            t["prevDistance"] = targets[i].prevDistance;
            t["prevAngle"] = targets[i].prevAngle;
            t["speed"] = targets[i].speed;
            t["age"] = millis() - targets[i].firstSeen;
            t["rssi"] = targets[i].rssi;
        }
    }
    doc["count"] = getActiveTargetCount();
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void handleZones() {
    StaticJsonDocument<512> doc;
    JsonArray arr = doc.createNestedArray("zones");
    
    for (uint8_t i = 0; i < MAX_ZONES; i++) {
        JsonObject z = arr.createNestedObject();
        z["id"] = i;
        z["enabled"] = zones[i].enabled;
        z["name"] = zones[i].name;
        z["minDist"] = zones[i].minDist;
        z["maxDist"] = zones[i].maxDist;
        z["minAngle"] = zones[i].minAngle;
        z["maxAngle"] = zones[i].maxAngle;
    }
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void handleStats() {
    StaticJsonDocument<512> doc;
    doc["totalDetections"] = config.totalDetections;
    doc["uptime"] = millis() - config.uptimeStart;
    doc["activeTargets"] = getActiveTargetCount();
    doc["alarmActive"] = config.alarmActive;
    doc["armed"] = config.armed;
    doc["wifiRSSI"] = WiFi.RSSI();
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["version"] = FIRMWARE_VERSION;
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void handleHistory() {
    StaticJsonDocument<2048> doc;
    JsonArray arr = doc.createNestedArray("events");
    
    uint8_t count = 0;
    for (uint8_t i = 0; i < HISTORY_SIZE && count < 20; i++) {
        int idx = (historyIndex - 1 - i + HISTORY_SIZE) % HISTORY_SIZE;
        if (history[idx].timestamp > 0) {
            JsonObject e = arr.createNestedObject();
            e["time"] = history[idx].timestamp;
            e["zone"] = history[idx].zoneId;
            e["targetId"] = history[idx].targetId;
            e["distance"] = history[idx].distance;
            e["angle"] = history[idx].angle;
            count++;
        }
    }
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void handleGetSettings() {
    StaticJsonDocument<512> doc;
    doc["detectionDistance"] = config.detectionDistance;
    doc["alarmDuration"] = config.alarmDuration;
    doc["sensitivity"] = config.sensitivity;
    doc["buzzerPattern"] = config.buzzerPattern;
    doc["soundEnabled"] = config.soundEnabled;
    doc["sweepSpeed"] = config.sweepSpeed;
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void handleSetSettings() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"no body\"}");
        return;
    }
    
    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    
    if (err) {
        server.send(400, "application/json", "{\"error\":\"invalid json\"}");
        return;
    }
    
    if (doc.containsKey("detectionDistance")) 
        config.detectionDistance = doc["detectionDistance"];
    if (doc.containsKey("alarmDuration")) 
        config.alarmDuration = doc["alarmDuration"];
    if (doc.containsKey("sensitivity")) 
        config.sensitivity = doc["sensitivity"];
    if (doc.containsKey("buzzerPattern")) 
        config.buzzerPattern = doc["buzzerPattern"];
    if (doc.containsKey("soundEnabled")) 
        config.soundEnabled = doc["soundEnabled"];
    if (doc.containsKey("sweepSpeed")) 
        config.sweepSpeed = doc["sweepSpeed"];
    
    saveSettings();
    server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleGetNotifications() {
    StaticJsonDocument<512> doc;
    doc["type"] = notifyConfig.type;
    doc["notifyOnDetection"] = notifyConfig.notifyOnDetection;
    doc["notifyOnAlarm"] = notifyConfig.notifyOnAlarm;
    doc["cooldownMinutes"] = notifyConfig.cooldownMinutes;
    doc["telegramChatId"] = notifyConfig.telegramChatId;
    doc["webhookUrl"] = notifyConfig.webhookUrl;
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void handleSetNotifications() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"no body\"}");
        return;
    }
    
    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    
    if (err) {
        server.send(400, "application/json", "{\"error\":\"invalid json\"}");
        return;
    }
    
    if (doc.containsKey("type")) 
        notifyConfig.type = (NotificationType)(int)doc["type"];
    if (doc.containsKey("notifyOnDetection")) 
        notifyConfig.notifyOnDetection = doc["notifyOnDetection"];
    if (doc.containsKey("notifyOnAlarm")) 
        notifyConfig.notifyOnAlarm = doc["notifyOnAlarm"];
    if (doc.containsKey("cooldownMinutes")) 
        notifyConfig.cooldownMinutes = doc["cooldownMinutes"];
    if (doc.containsKey("telegramChatId")) 
        strlcpy(notifyConfig.telegramChatId, doc["telegramChatId"], 32);
    if (doc.containsKey("webhookUrl")) 
        strlcpy(notifyConfig.webhookUrl, doc["webhookUrl"], 128);
    
    saveNotificationSettings();
    server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleArm() {
    config.armed = true;
    DEBUG_PRINTLN(F("[API] System armed"));
    server.send(200, "application/json", "{\"status\":\"armed\"}");
}

void handleDisarm() {
    config.armed = false;
    config.alarmActive = false;
    DEBUG_PRINTLN(F("[API] System disarmed"));
    server.send(200, "application/json", "{\"status\":\"disarmed\"}");
}

void handleTestBuzzer() {
    for (uint8_t i = 0; i < 3; i++) {
        digitalWrite(PIN_BUZZER, HIGH);
        delay(100);
        digitalWrite(PIN_BUZZER, LOW);
        delay(100);
    }
    server.send(200, "application/json", "{\"status\":\"tested\"}");
}

void handleTestNotification() {
    DEBUG_PRINTLN(F("[API] Test notification requested"));
    DEBUG_PRINT(F("[API] Type: "));
    DEBUG_PRINTLN(notifyConfig.type);
    DEBUG_PRINT(F("[API] Token: "));
    DEBUG_PRINTLN(strlen(notifyConfig.telegramBotToken) > 0 ? "Set" : "Empty");
    DEBUG_PRINT(F("[API] ChatID: "));
    DEBUG_PRINTLN(strlen(notifyConfig.telegramChatId) > 0 ? "Set" : "Empty");
    
    // Reset cooldown and force send
    notifyConfig.lastNotification = 0;
    bool result = sendNotification("Test", "ESP32 Radar test notification", true);
    if (result) {
        server.send(200, "application/json", "{\"status\":\"sent\"}");
    } else {
        server.send(500, "application/json", "{\"status\":\"failed\",\"type\":" + String(notifyConfig.type) + "}");
    }
}

// ============================================================================
// WEB UI GENERATION
// ============================================================================

String generateWebUI() {
    return F(R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 Radar Security System</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        :root {
            --primary: #00ff88;
            --danger: #ff4444;
            --warning: #ffaa00;
            --bg: #0a0a0f;
            --panel: #12121a;
        }
        body {
            background: var(--bg);
            color: var(--primary);
            font-family: 'Segoe UI', system-ui, sans-serif;
            min-height: 100vh;
            padding: 15px;
        }
        .header {
            text-align: center;
            margin-bottom: 20px;
            padding: 20px;
            background: linear-gradient(135deg, rgba(0,255,136,0.1), transparent);
            border-radius: 15px;
            border: 1px solid rgba(0,255,136,0.2);
        }
        .header h1 {
            font-size: 26px;
            letter-spacing: 3px;
            text-shadow: 0 0 20px var(--primary);
            margin-bottom: 5px;
        }
        .version {
            font-size: 11px;
            opacity: 0.6;
            letter-spacing: 2px;
        }
        .status-bar {
            display: flex;
            justify-content: center;
            gap: 15px;
            flex-wrap: wrap;
            margin-top: 15px;
        }
        .status-pill {
            padding: 8px 20px;
            border-radius: 20px;
            font-size: 11px;
            text-transform: uppercase;
            letter-spacing: 2px;
            background: rgba(0,255,136,0.1);
            border: 1px solid var(--primary);
            transition: all 0.3s;
        }
        .status-pill.active {
            background: rgba(255,68,68,0.2);
            border-color: var(--danger);
            color: var(--danger);
            box-shadow: 0 0 15px rgba(255,68,68,0.4);
            animation: pulse 1s infinite;
        }
        @keyframes pulse {
            0%, 100% { opacity: 1; }
            50% { opacity: 0.7; }
        }
        .main-grid {
            display: grid;
            grid-template-columns: 1fr 380px;
            gap: 20px;
            max-width: 1400px;
            margin: 0 auto;
        }
        @media (max-width: 1000px) {
            .main-grid { grid-template-columns: 1fr; }
        }
        .radar-section {
            background: var(--panel);
            border-radius: 20px;
            padding: 20px;
            border: 1px solid rgba(0,255,136,0.15);
        }
        .radar-container {
            position: relative;
            width: 100%;
            max-width: 600px;
            margin: 0 auto;
            aspect-ratio: 2/1;
            background: radial-gradient(ellipse at center bottom, rgba(0,50,0,0.3) 0%, transparent 70%);
            border-radius: 50% 50% 10px 10px / 100% 100% 10px 10px;
            border: 2px solid var(--primary);
            box-shadow: 0 0 30px rgba(0,255,136,0.15), inset 0 0 60px rgba(0,255,136,0.05);
            overflow: hidden;
        }
        #radar { width: 100%; height: 100%; display: block; }
        .metrics {
            display: grid;
            grid-template-columns: repeat(3, 1fr);
            gap: 15px;
            margin-top: 20px;
        }
        .metric-card {
            background: rgba(0,255,136,0.05);
            border: 1px solid rgba(0,255,136,0.2);
            border-radius: 12px;
            padding: 15px;
            text-align: center;
        }
        .metric-value {
            font-size: 28px;
            font-weight: bold;
            text-shadow: 0 0 10px var(--primary);
        }
        .metric-label {
            font-size: 10px;
            text-transform: uppercase;
            letter-spacing: 2px;
            opacity: 0.7;
            margin-top: 5px;
        }
        .sidebar { display: flex; flex-direction: column; gap: 15px; }
        .panel {
            background: var(--panel);
            border-radius: 15px;
            padding: 18px;
            border: 1px solid rgba(0,255,136,0.15);
        }
        .panel h3 {
            font-size: 13px;
            text-transform: uppercase;
            letter-spacing: 2px;
            margin-bottom: 15px;
            padding-bottom: 10px;
            border-bottom: 1px solid rgba(0,255,136,0.2);
            display: flex;
            align-items: center;
            gap: 8px;
        }
        .control-group { margin-bottom: 15px; }
        .control-group label {
            display: block;
            font-size: 11px;
            text-transform: uppercase;
            opacity: 0.8;
            margin-bottom: 6px;
        }
        .control-group input[type="range"] {
            width: 100%;
            accent-color: var(--primary);
        }
        .control-group select, .control-group button, .control-group input[type="text"] {
            width: 100%;
            padding: 12px;
            background: rgba(0,255,136,0.08);
            border: 1px solid var(--primary);
            color: var(--primary);
            border-radius: 8px;
            cursor: pointer;
            font-size: 12px;
            text-transform: uppercase;
            letter-spacing: 1px;
            transition: all 0.2s;
        }
        .control-group button:hover {
            background: rgba(0,255,136,0.2);
            box-shadow: 0 0 15px rgba(0,255,136,0.2);
        }
        .control-group button.danger {
            background: rgba(255,68,68,0.1);
            border-color: var(--danger);
            color: var(--danger);
        }
        .control-group button.danger:hover {
            background: rgba(255,68,68,0.2);
            box-shadow: 0 0 15px rgba(255,68,68,0.3);
        }
        .target-list { max-height: 200px; overflow-y: auto; }
        .target-item {
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding: 12px;
            background: rgba(255,68,68,0.1);
            border: 1px solid rgba(255,68,68,0.3);
            border-radius: 8px;
            margin-bottom: 8px;
            font-size: 12px;
        }
        .target-id { color: #ff8888; font-weight: bold; font-size: 14px; }
        .zone-legend { display: flex; flex-direction: column; gap: 10px; }
        .zone-item {
            display: flex;
            align-items: center;
            gap: 12px;
            font-size: 12px;
            padding: 8px;
            background: rgba(0,255,136,0.05);
            border-radius: 6px;
        }
        .zone-color { width: 14px; height: 14px; border-radius: 3px; }
        .stats-grid {
            display: grid;
            grid-template-columns: repeat(2, 1fr);
            gap: 12px;
        }
        .stat-item {
            text-align: center;
            padding: 15px;
            background: rgba(0,255,136,0.05);
            border-radius: 10px;
        }
        .stat-value { font-size: 22px; font-weight: bold; }
        .stat-label { font-size: 10px; opacity: 0.7; text-transform: uppercase; letter-spacing: 1px; margin-top: 4px; }
        .checkbox-label {
            display: flex !important;
            align-items: center;
            gap: 10px;
            cursor: pointer;
        }
        .checkbox-label input[type="checkbox"] {
            width: 18px;
            height: 18px;
            accent-color: var(--primary);
        }
    </style>
</head>
<body>
    <div class="header">
        <h1>ESP32 RADAR SECURITY</h1>
        <div class="version">PROFESSIONAL EDITION v2.0.0</div>
        <div class="status-bar">
            <span id="armStatus" class="status-pill">DISARMED</span>
            <span id="alarmStatus" class="status-pill">CLEAR</span>
            <span id="targetCount" class="status-pill">0 TARGETS</span>
        </div>
    </div>
    <div class="main-grid">
        <div class="radar-section">
            <div class="radar-container">
                <canvas id="radar"></canvas>
            </div>
            <div class="metrics">
                <div class="metric-card">
                    <div class="metric-value" id="distVal">--</div>
                    <div class="metric-label">Distance (m)</div>
                </div>
                <div class="metric-card">
                    <div class="metric-value" id="angleVal">--</div>
                    <div class="metric-label">Angle</div>
                </div>
                <div class="metric-card">
                    <div class="metric-value" id="zoneVal">--</div>
                    <div class="metric-label">Zone</div>
                </div>
            </div>
        </div>
        <div class="sidebar">
            <div class="panel">
                <h3>System Control</h3>
                <div class="control-group">
                    <button id="armBtn" onclick="toggleArm()">ARM SYSTEM</button>
                </div>
                <div class="control-group">
                    <button onclick="testBuzzer()">Test Buzzer</button>
                </div>
            </div>
            <div class="panel">
                <h3>Detection Settings</h3>
                <div class="control-group">
                    <label>Range: <span id="rangeVal">3.0</span>m</label>
                    <input type="range" id="rangeSlider" min="500" max="5000" value="3000" onchange="updateSettings()">
                </div>
                <div class="control-group">
                    <label>Duration: <span id="durVal">10</span>s</label>
                    <input type="range" id="durSlider" min="1" max="60" value="10" onchange="updateSettings()">
                </div>
                <div class="control-group">
                    <label>Buzzer Pattern</label>
                    <select id="patternSelect" onchange="updateSettings()">
                        <option value="0">Continuous</option>
                        <option value="1">Pulse</option>
                        <option value="2">SOS</option>
                        <option value="3">Beep</option>
                        <option value="4">Warble</option>
                    </select>
                </div>
            </div>
            <div class="panel">
                <h3>Active Targets</h3>
                <div class="target-list" id="targetList">
                    <div style="text-align:center; opacity:0.5; padding:20px;">No targets detected</div>
                </div>
            </div>
            <div class="panel">
                <h3>Notifications</h3>
                <div class="control-group">
                    <label>Service</label>
                    <select id="notifyType" onchange="updateNotifications()">
                        <option value="0">None</option>
                        <option value="1">Telegram</option>
                        <option value="2">Discord</option>
                        <option value="4">Webhook</option>
                        <option value="5">Buzzer Only</option>
                    </select>
                </div>
                <div class="control-group">
                    <label>Cooldown: <span id="cooldownVal">5</span> min</label>
                    <input type="range" id="cooldownSlider" min="1" max="60" value="5" onchange="updateNotifications()">
                </div>
                <div class="control-group">
                    <label class="checkbox-label">
                        <input type="checkbox" id="notifyAlarm" onchange="updateNotifications()">
                        Notify on Alarm
                    </label>
                </div>
                <div id="telegramConfig" style="display:none;">
                    <div class="control-group">
                        <label>Chat ID</label>
                        <input type="text" id="tgChatId" placeholder="-100123...">
                    </div>
                </div>
                <div id="webhookConfig" style="display:none;">
                    <div class="control-group">
                        <label>Webhook URL</label>
                        <input type="text" id="webhookUrl" placeholder="https://...">
                    </div>
                </div>
                <div class="control-group">
                    <button onclick="saveNotifications()">Save Settings</button>
                </div>
                <div class="control-group">
                    <button onclick="testNotification()">Test Notification</button>
                </div>
            </div>
            <div class="panel">
                <h3>Security Zones</h3>
                <div class="zone-legend">
                    <div class="zone-item"><div class="zone-color" style="background:#00ff88;"></div> Front Zone (0-1m, 45-135)</div>
                    <div class="zone-item"><div class="zone-color" style="background:#0088ff;"></div> Left Zone (0-2m, 0-45)</div>
                    <div class="zone-item"><div class="zone-color" style="background:#ff8800;"></div> Right Zone (0-2m, 135-180)</div>
                </div>
            </div>
            <div class="panel">
                <h3>System Statistics</h3>
                <div class="stats-grid">
                    <div class="stat-item"><div class="stat-value" id="totalDetections">0</div><div class="stat-label">Total Detections</div></div>
                    <div class="stat-item"><div class="stat-value" id="uptime">0m</div><div class="stat-label">Uptime</div></div>
                </div>
            </div>
        </div>
    </div>
    <script>
        const canvas = document.getElementById('radar');
        const ctx = canvas.getContext('2d');
        let cx, cy, R, maxRange = 3000;
        let sweepAngle = 0;
        let targets = [];
        let isArmed = false;
        
        const zones = [
            {name:'Front', minA:45, maxA:135, minD:0, maxD:1000, color:'#00ff88'},
            {name:'Left', minA:0, maxA:45, minD:0, maxD:2000, color:'#0088ff'},
            {name:'Right', minA:135, maxA:180, minD:0, maxD:2000, color:'#ff8800'}
        ];
        
        function resize() {
            const rect = canvas.parentElement.getBoundingClientRect();
            canvas.width = rect.width;
            canvas.height = rect.height;
            cx = canvas.width / 2;
            cy = canvas.height;
            R = Math.min(canvas.width, canvas.height * 2) * 0.42;
        }
        window.addEventListener('resize', resize);
        resize();
        
        function drawGrid() {
            ctx.fillStyle = 'rgba(0,0,0,0.1)';
            ctx.fillRect(0, 0, canvas.width, canvas.height);
            ctx.strokeStyle = 'rgba(0, 255, 136, 0.15)';
            ctx.lineWidth = 1;
            
            for(let i = 1; i <= 4; i++) {
                ctx.beginPath();
                ctx.arc(cx, cy, R * i/4, Math.PI, 0);
                ctx.stroke();
                ctx.fillStyle = 'rgba(0, 255, 136, 0.5)';
                ctx.font = '10px monospace';
                ctx.textAlign = 'center';
                ctx.fillText((maxRange/4000*i).toFixed(1)+'m', cx, cy - R*i/4 - 3);
            }
            
            for(let a = 0; a <= 180; a += 30) {
                let rad = (a-90) * Math.PI/180;
                ctx.beginPath();
                ctx.moveTo(cx, cy);
                ctx.lineTo(cx + R*Math.cos(rad), cy + R*Math.sin(rad));
                ctx.stroke();
            }
            
            ctx.beginPath();
            ctx.arc(cx, cy, 5, 0, Math.PI*2);
            ctx.fillStyle = '#00ff88';
            ctx.fill();
        }
        
        function drawZones() {
            zones.forEach(z => {
                ctx.beginPath();
                ctx.arc(cx, cy, z.maxD/maxRange*R, (z.minA-90)*Math.PI/180, (z.maxA-90)*Math.PI/180, true);
                ctx.arc(cx, cy, z.minD/maxRange*R, (z.maxA-90)*Math.PI/180, (z.minA-90)*Math.PI/180, false);
                ctx.closePath();
                ctx.fillStyle = z.color + '15';
                ctx.fill();
                ctx.strokeStyle = z.color + '40';
                ctx.stroke();
            });
        }
        
        function drawSweep() {
            let rad = (sweepAngle-90) * Math.PI/180;
            let grad = ctx.createConicGradient(rad+Math.PI/2, cx, cy);
            grad.addColorStop(0, 'rgba(0,255,136,0)');
            grad.addColorStop(0.7, 'rgba(0,255,136,0)');
            grad.addColorStop(0.95, 'rgba(0,255,136,0.4)');
            grad.addColorStop(1, 'rgba(0,255,136,0.8)');
            ctx.fillStyle = grad;
            ctx.beginPath();
            ctx.moveTo(cx, cy);
            ctx.arc(cx, cy, R, Math.PI, 0);
            ctx.closePath();
            ctx.fill();
            sweepAngle = (sweepAngle + 2) % 180;
        }
        
        function drawTarget(t) {
            let r = (t.distance/maxRange) * R;
            let rad = (t.angle-90) * Math.PI/180;
            let x = cx + r*Math.cos(rad);
            let y = cy + r*Math.sin(rad);
            let age = Date.now() - t.seen;
            let fade = Math.max(0, 1 - age/3000);
            let pulse = Math.sin(Date.now()/80) * 0.3 + 0.7;
            
            if(t.prevDistance > 0 && t.prevAngle > 0) {
                let pr = (t.prevDistance/maxRange) * R;
                let prad = (t.prevAngle-90) * Math.PI/180;
                let px = cx + pr*Math.cos(prad);
                let py = cy + pr*Math.sin(prad);
                let trailGrad = ctx.createLinearGradient(px, py, x, y);
                trailGrad.addColorStop(0, 'rgba(255,100,100,0)');
                trailGrad.addColorStop(0.5, 'rgba(255,100,100,'+(0.5*fade)+')');
                trailGrad.addColorStop(1, 'rgba(255,50,50,'+(0.8*fade)+')');
                ctx.strokeStyle = trailGrad;
                ctx.lineWidth = 3;
                ctx.beginPath();
                ctx.moveTo(px, py);
                ctx.lineTo(x, y);
                ctx.stroke();
            }
            
            let glow = ctx.createRadialGradient(x, y, 0, x, y, 25*fade*pulse);
            glow.addColorStop(0, 'rgba(255,50,50,1)');
            glow.addColorStop(0.4, 'rgba(255,0,0,'+(0.6*fade)+')');
            glow.addColorStop(1, 'rgba(255,0,0,0)');
            ctx.fillStyle = glow;
            ctx.beginPath();
            ctx.arc(x, y, 25*fade*pulse, 0, Math.PI*2);
            ctx.fill();
            
            ctx.fillStyle = '#fff';
            ctx.beginPath();
            ctx.arc(x, y, 4, 0, Math.PI*2);
            ctx.fill();
            
            ctx.strokeStyle = 'rgba(255,100,100,0.6)';
            ctx.lineWidth = 2;
            ctx.beginPath();
            ctx.arc(x, y, 12*pulse, 0, Math.PI*2);
            ctx.stroke();
            
            ctx.fillStyle = '#ff6666';
            ctx.font = 'bold 11px monospace';
            ctx.textAlign = 'center';
            ctx.fillText('T'+t.id, x, y-18);
            
            if(t.speed && Math.abs(t.speed) > 0) {
                ctx.fillStyle = '#ffff00';
                ctx.font = '10px monospace';
                ctx.fillText((t.speed > 0 ? '+' : '') + t.speed + 'cm/s', x, y+25);
            }
        }
        
        function animate() {
            drawGrid();
            drawZones();
            targets.forEach(t => drawTarget(t));
            drawSweep();
            requestAnimationFrame(animate);
        }
        
        function updateUI(data) {
            document.getElementById('distVal').innerText = data.distance > 0 ? (data.distance/1000).toFixed(2) : '--';
            document.getElementById('angleVal').innerText = data.angle > 0 ? data.angle.toFixed(1) : '--';
            document.getElementById('zoneVal').innerText = data.zone >= 0 ? zones[data.zone]?.name || 'Z'+data.zone : '--';
            
            let armPill = document.getElementById('armStatus');
            armPill.innerText = data.armed ? 'ARMED' : 'DISARMED';
            armPill.className = 'status-pill' + (data.armed ? ' active' : '');
            
            let alarmPill = document.getElementById('alarmStatus');
            alarmPill.innerText = data.alarmActive ? 'ALARM TRIGGERED' : 'CLEAR';
            alarmPill.className = 'status-pill' + (data.alarmActive ? ' active' : '');
            
            document.getElementById('armBtn').innerText = data.armed ? 'DISARM SYSTEM' : 'ARM SYSTEM';
            document.getElementById('armBtn').className = data.armed ? 'danger' : '';
            isArmed = data.armed;
        }
        
        function updateTargets(data) {
            document.getElementById('targetCount').innerText = data.count + ' TARGET' + (data.count!==1?'S':'');
            if(data.count > 0) document.getElementById('targetCount').classList.add('active');
            else document.getElementById('targetCount').classList.remove('active');
            
            targets = data.targets.map(t => ({...t, seen: Date.now()}));
            
            let html = '';
            data.targets.forEach(t => {
                html += `<div class="target-item">
                    <span class="target-id">T${t.id}</span>
                    <span>${(t.distance/1000).toFixed(2)}m @ ${t.angle.toFixed(0)}</span>
                </div>`;
            });
            document.getElementById('targetList').innerHTML = html || '<div style="text-align:center; opacity:0.5; padding:20px;">No targets detected</div>';
        }
        
        function updateStats(data) {
            document.getElementById('totalDetections').innerText = data.totalDetections;
            document.getElementById('uptime').innerText = Math.floor(data.uptime/60000) + 'm';
        }
        
        function toggleArm() {
            fetch(isArmed ? '/api/disarm' : '/api/arm', {method:'POST'});
        }
        
        function testBuzzer() {
            fetch('/api/test-buzzer', {method:'POST'});
        }
        
        function updateSettings() {
            let range = document.getElementById('rangeSlider').value;
            let duration = document.getElementById('durSlider').value;
            let pattern = document.getElementById('patternSelect').value;
            document.getElementById('rangeVal').innerText = (range/1000).toFixed(1);
            document.getElementById('durVal').innerText = duration;
            
            fetch('/api/settings', {
                method: 'POST',
                headers: {'Content-Type':'application/json'},
                body: JSON.stringify({
                    detectionDistance: parseInt(range),
                    alarmDuration: parseInt(duration) * 1000,
                    buzzerPattern: parseInt(pattern)
                })
            });
        }
        
        function updateNotifications() {
            document.getElementById('cooldownVal').innerText = document.getElementById('cooldownSlider').value;
            let type = parseInt(document.getElementById('notifyType').value);
            document.getElementById('telegramConfig').style.display = (type === 1) ? 'block' : 'none';
            document.getElementById('webhookConfig').style.display = (type === 4) ? 'block' : 'none';
        }
        
        function saveNotifications() {
            let data = {
                type: parseInt(document.getElementById('notifyType').value),
                cooldownMinutes: parseInt(document.getElementById('cooldownSlider').value),
                notifyOnAlarm: document.getElementById('notifyAlarm').checked,
                telegramChatId: document.getElementById('tgChatId').value,
                webhookUrl: document.getElementById('webhookUrl').value
            };
            fetch('/api/notifications', {
                method: 'POST',
                headers: {'Content-Type':'application/json'},
                body: JSON.stringify(data)
            }).then(r => r.json()).then(res => {
                if(res.status === 'ok') alert('Settings saved!');
            });
        }
        
        function testNotification() {
            fetch('/api/test-notification', {method:'POST'})
                .then(r => r.json())
                .then(res => alert(res.status === 'sent' ? 'Notification sent!' : 'Failed'));
        }
        
        function loadSettings() {
            fetch('/api/settings').then(r => r.json()).then(s => {
                document.getElementById('rangeSlider').value = s.detectionDistance;
                document.getElementById('durSlider').value = s.alarmDuration / 1000;
                document.getElementById('patternSelect').value = s.buzzerPattern;
                document.getElementById('rangeVal').innerText = (s.detectionDistance/1000).toFixed(1);
                document.getElementById('durVal').innerText = s.alarmDuration / 1000;
            });
            fetch('/api/notifications').then(r => r.json()).then(n => {
                document.getElementById('notifyType').value = n.type;
                document.getElementById('cooldownSlider').value = n.cooldownMinutes;
                document.getElementById('cooldownVal').innerText = n.cooldownMinutes;
                document.getElementById('notifyAlarm').checked = n.notifyOnAlarm;
                document.getElementById('tgChatId').value = n.telegramChatId || '';
                document.getElementById('webhookUrl').value = n.webhookUrl || '';
                updateNotifications();
            });
        }
        
        setInterval(() => {
            fetch('/api/radar-data').then(r => r.json()).then(updateUI);
            fetch('/api/targets').then(r => r.json()).then(updateTargets);
            fetch('/api/stats').then(r => r.json()).then(updateStats);
        }, 200);
        
        loadSettings();
        animate();
    </script>
</body>
</html>
)rawliteral");
}
