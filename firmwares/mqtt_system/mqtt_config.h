/**
 * mqtt_config.h
 * MQTT broker settings and topic definitions for Smart Helmet System
 */

#ifndef MQTT_CONFIG_H
#define MQTT_CONFIG_H

// ---------------------------------------------------------------------------
// WiFi credentials  (update before flashing)
// ---------------------------------------------------------------------------
#define WIFI_SSID     "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// ---------------------------------------------------------------------------
// MQTT broker  (HiveMQ public broker or local Mosquitto)
// ---------------------------------------------------------------------------
#define MQTT_BROKER   "broker.hivemq.com"
#define MQTT_PORT     1883

// Optional authentication (leave empty if not used)
#define MQTT_USER     ""
#define MQTT_PASSWORD_BROKER ""

// Unique client IDs – make sure these are different on every device
#define MQTT_CLIENT_ESP32     "SmartHelmet_ESP32"
#define MQTT_CLIENT_CAM       "SmartHelmet_CAM"
#define MQTT_CLIENT_SERVER    "SmartHelmet_Server"

// ---------------------------------------------------------------------------
// Topics – sensor data (ESP32 publishes)
// ---------------------------------------------------------------------------
#define TOPIC_GPS           "helmet/sensors/gps"
#define TOPIC_HEART_RATE    "helmet/sensors/heart_rate"
#define TOPIC_SPO2          "helmet/sensors/spo2"
#define TOPIC_ALCOHOL       "helmet/sensors/alcohol"
#define TOPIC_LEAN_ANGLE    "helmet/sensors/lean_angle"
#define TOPIC_PRESSURE      "helmet/sensors/pressure"
#define TOPIC_ALTITUDE      "helmet/sensors/altitude"
#define TOPIC_CRASH         "helmet/sensors/crash"

// ---------------------------------------------------------------------------
// Topics – eye / fatigue data (ESP32-CAM publishes)
// ---------------------------------------------------------------------------
#define TOPIC_BLINK_RATE    "helmet/eye/blink_rate"
#define TOPIC_EYE_CLOSURE   "helmet/eye/closure_duration"
#define TOPIC_EAR           "helmet/eye/ear"          // Eye Aspect Ratio
#define TOPIC_FATIGUE       "helmet/eye/fatigue"

// ---------------------------------------------------------------------------
// Topics – aggregated alerts (both devices publish, server subscribes)
// ---------------------------------------------------------------------------
#define TOPIC_ALERT         "helmet/alert"
#define TOPIC_ALERT_TYPE    "helmet/alert/type"

// ---------------------------------------------------------------------------
// Topics – commands (server / dashboard publishes, ESP32 subscribes)
// ---------------------------------------------------------------------------
#define TOPIC_CMD_THRESHOLD "helmet/cmd/threshold"
#define TOPIC_CMD_CONTACT   "helmet/cmd/contact"
#define TOPIC_CMD_RESET     "helmet/cmd/reset"

// ---------------------------------------------------------------------------
// QoS levels
// ---------------------------------------------------------------------------
#define MQTT_QOS_SENSOR  0   // Fire-and-forget for high-frequency data
#define MQTT_QOS_ALERT   1   // At-least-once for alerts

// ---------------------------------------------------------------------------
// Reconnect / publish intervals (ms)
// ---------------------------------------------------------------------------
#define MQTT_RECONNECT_INTERVAL  5000
#define SENSOR_PUBLISH_INTERVAL  1000   // 1 s
#define EYE_PUBLISH_INTERVAL      500   // 0.5 s

#endif // MQTT_CONFIG_H
