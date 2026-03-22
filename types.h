
/*
 * ESP32 Radar Security System - Type Definitions
 * 
 * Author: Akshay Rathore
 * Version: 2.0.0
 */

#ifndef TYPES_H
#define TYPES_H

#include <Arduino.h>
#include "config.h"

// ============================================================================
// DATA STRUCTURES
// ============================================================================

/**
 * @brief Security zone configuration
 */
struct Zone {
    bool enabled;
    uint16_t minDist;
    uint16_t maxDist;
    uint16_t minAngle;
    uint16_t maxAngle;
    char name[16];
    
    // Constructor for brace initialization
    Zone() : enabled(false), minDist(0), maxDist(0), minAngle(0), maxAngle(0) {
        name[0] = '\0';
    }
    
    Zone(bool e, uint16_t minD, uint16_t maxD, uint16_t minA, uint16_t maxA, const char* n) 
        : enabled(e), minDist(minD), maxDist(maxD), minAngle(minA), maxAngle(maxA) {
        strncpy(name, n, 15);
        name[15] = '\0';
    }
};

/**
 * @brief Tracked target information
 */
struct Target {
    bool active;
    uint16_t id;
    uint16_t distance;
    float angle;
    float prevDistance;
    float prevAngle;
    float speed;
    float direction;
    uint32_t firstSeen;
    uint32_t lastSeen;
    int16_t rssi;
};

/**
 * @brief Detection event for history logging
 */
struct DetectionEvent {
    uint32_t timestamp;
    int8_t zoneId;
    uint16_t targetId;
    uint16_t distance;
    float angle;
};

/**
 * @brief System configuration stored in preferences
 */
struct SystemConfig {
    bool armed;
    bool systemEnabled;
    bool alarmActive;
    bool soundEnabled;
    uint16_t detectionDistance;
    uint16_t alarmDuration;
    uint16_t sensitivity;
    uint8_t buzzerPattern;
    uint8_t sweepSpeed;
    uint32_t totalDetections;
    uint32_t uptimeStart;
    uint32_t alarmStartTime;
    
    // Constructor with defaults
    SystemConfig() : 
        armed(true),
        systemEnabled(true),
        alarmActive(false),
        soundEnabled(true),
        detectionDistance(DEFAULT_DETECTION_DISTANCE_MM),
        alarmDuration(DEFAULT_ALARM_DURATION_MS),
        sensitivity(DEFAULT_SENSITIVITY),
        buzzerPattern(PATTERN_CONTINUOUS),
        sweepSpeed(DEFAULT_SWEEP_SPEED),
        totalDetections(0),
        uptimeStart(0),
        alarmStartTime(0)
    {}
};

/**
 * @brief Notification service configuration
 */
struct NotificationConfig {
    NotificationType type;
    char telegramBotToken[64];
    char telegramChatId[32];
    char discordWebhook[128];
    char emailServer[64];
    char emailUser[64];
    char emailPass[64];
    char webhookUrl[128];
    bool notifyOnDetection;
    bool notifyOnAlarm;
    uint8_t cooldownMinutes;
    uint32_t lastNotification;
    
    // Constructor with defaults
    NotificationConfig() :
        type(NOTIFY_TELEGRAM),
        notifyOnDetection(false),
        notifyOnAlarm(true),
        cooldownMinutes(DEFAULT_NOTIFICATION_COOLDOWN),
        lastNotification(0)
    {
        memset(telegramBotToken, 0, sizeof(telegramBotToken));
        memset(telegramChatId, 0, sizeof(telegramChatId));
        memset(discordWebhook, 0, sizeof(discordWebhook));
        memset(emailServer, 0, sizeof(emailServer));
        memset(emailUser, 0, sizeof(emailUser));
        memset(emailPass, 0, sizeof(emailPass));
        memset(webhookUrl, 0, sizeof(webhookUrl));
    }
};

/**
 * @brief Radar raw data frame
 */
struct RadarFrame {
    uint8_t buffer[RADAR_FRAME_SIZE];
    uint8_t index;
    bool valid;
    
    RadarFrame() : index(0), valid(false) {
        memset(buffer, 0, RADAR_FRAME_SIZE);
    }
};

/**
 * @brief System status for API responses
 */
struct SystemStatus {
    bool armed;
    bool alarmActive;
    bool systemEnabled;
    uint16_t distance;
    float angle;
    int8_t zone;
    bool targetDetected;
    uint8_t activeTargets;
    int16_t wifiRssi;
    uint32_t freeHeap;
    uint32_t uptime;
    uint32_t totalDetections;
};

#endif // TYPES_H
