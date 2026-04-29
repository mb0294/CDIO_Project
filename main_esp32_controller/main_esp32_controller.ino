/**
 * main_esp32_controller.ino
 *
 * Smart Helmet – Main ESP32 Sensor Controller
 * -------------------------------------------
 * Reads all helmet sensors and publishes data via MQTT.
 * Triggers local (buzzer) and remote (SMS via SIM800L) alerts.
 *
 * Sensors:
 *   GPS     – NEO-6M   (UART1, pins 16/17)
 *   Heart   – MAX30102 (I2C, 0x57)
 *   Gyro    – MPU-9250 (I2C, 0x68)
 *   Alcohol – MQ-3     (ADC pin 34)
 *   Pressure– Analog   (ADC pin 35)
 *   SMS     – SIM800L  (UART2, pins 26/27)
 *
 * Libraries required (install via Arduino Library Manager):
 *   - TinyGPSPlus    (Mikal Hart)
 *   - SparkFun MAX3010x Pulse and Proximity Sensor Library
 *   - MPU9250        (hideakitai)
 *   - PubSubClient   (Nick O'Leary)
 *   - ArduinoJson    (Benoit Blanchon)
 */

#include <Wire.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <TinyGPS++.h>
#include <HardwareSerial.h>
#include "MAX30105.h"
#include "heartRate.h"
#include <MPU9250.h>
#include "mqtt_config.h"
#include "sensor_config.h"

// ---------------------------------------------------------------------------
// Hardware serial ports
// ---------------------------------------------------------------------------
HardwareSerial gpsSerial(1);    // GPS  – UART1
HardwareSerial sim800Serial(2); // SIM800L – UART2

// ---------------------------------------------------------------------------
// Sensor objects
// ---------------------------------------------------------------------------
TinyGPSPlus  gps;
MAX30105     heartSensor;
MPU9250      mpu;

// ---------------------------------------------------------------------------
// MQTT
// ---------------------------------------------------------------------------
WiFiClient   wifiClient;
PubSubClient mqttClient(wifiClient);

// ---------------------------------------------------------------------------
// State / timing
// ---------------------------------------------------------------------------
unsigned long g_lastSensorRead   = 0;
unsigned long g_lastMqttPublish  = 0;
unsigned long g_lastSmsSent      = 0;

// Sensor values
float   g_latitude     = 0.0f;
float   g_longitude    = 0.0f;
float   g_altitude_gps = 0.0f;
int     g_heartRate    = 0;
int     g_spo2         = 0;
int     g_alcoholRaw   = 0;
float   g_alcoholPct   = 0.0f;
float   g_leanAngle    = 0.0f;
float   g_accelX       = 0.0f;
float   g_accelY       = 0.0f;
float   g_accelZ       = 0.0f;
int     g_pressureRaw  = 0;
float   g_altitude_bar = 0.0f;

// Alert flags
bool    g_crashDetected       = false;
bool    g_alcoholAlert        = false;
bool    g_heartRateAlert      = false;
bool    g_leanAngleAlert      = false;
bool    g_fatigue             = false;  // set via MQTT from ESP32-CAM

// Heart-rate ring buffer (SparkFun library style)
const byte RATE_SIZE = 4;
byte   rates[RATE_SIZE];
byte   rateSpot    = 0;
long   lastBeat    = 0;
float  beatsPerMinute = 0;
int    beatAvg        = 0;

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
void connectWiFi();
void connectMQTT();
void mqttCallback(char* topic, byte* payload, unsigned int len);
void readGPS();
void readHeartRate();
void readGyroscope();
void readAlcohol();
void readPressure();
void publishSensorData();
void checkAlerts();
void triggerAlert(const char* alertType, const char* message);
void sendSMS(const char* number, const char* message);
void buzzAlert(int count);
String buildSMSMessage(const char* alertType);

// ===========================================================================
// setup()
// ===========================================================================
void setup() {
  Serial.begin(115200);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(LED_PIN, LOW);

  // I2C
  Wire.begin(I2C_SDA, I2C_SCL);

  // GPS UART
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

  // SIM800L UART
  sim800Serial.begin(SIM800_BAUD, SERIAL_8N1, SIM800_RX_PIN, SIM800_TX_PIN);
  delay(1000);
  sim800Serial.println("AT");
  delay(500);
  sim800Serial.println("AT+CMGF=1");  // SMS text mode
  delay(500);

  // MAX30102
  if (!heartSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("[HEART] MAX30102 not found – check wiring");
  } else {
    heartSensor.setup();
    heartSensor.setPulseAmplitudeRed(0x0A);
    heartSensor.setPulseAmplitudeGreen(0);
    Serial.println("[HEART] MAX30102 ready");
  }

  // MPU-9250
  if (!mpu.setup(MPU_I2C_ADDR)) {
    Serial.println("[GYRO] MPU-9250 not found – check wiring");
  } else {
    Serial.println("[GYRO] MPU-9250 ready");
  }

  connectWiFi();

  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  connectMQTT();

  Serial.println("[MAIN] Smart Helmet controller ready ✓");
}

// ===========================================================================
// loop()
// ===========================================================================
void loop() {
  if (!mqttClient.connected()) {
    connectMQTT();
  }
  mqttClient.loop();

  // Feed GPS decoder with available bytes
  while (gpsSerial.available()) {
    gps.encode(gpsSerial.read());
  }

  unsigned long now = millis();
  if (now - g_lastSensorRead >= SENSOR_READ_INTERVAL_MS) {
    g_lastSensorRead = now;
    readGPS();
    readHeartRate();
    readGyroscope();
    readAlcohol();
    readPressure();
    checkAlerts();
  }

  if (now - g_lastMqttPublish >= SENSOR_PUBLISH_INTERVAL) {
    g_lastMqttPublish = now;
    publishSensorData();
  }
}

// ===========================================================================
// WiFi / MQTT
// ===========================================================================
void connectWiFi() {
  Serial.printf("[WIFI] Connecting to %s", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WIFI] Connected. IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\n[WIFI] Connection failed – continuing offline");
  }
}

void connectMQTT() {
  static unsigned long lastAttempt = 0;
  if (millis() - lastAttempt < MQTT_RECONNECT_INTERVAL) return;
  lastAttempt = millis();

  Serial.printf("[MQTT] Connecting to %s:%d ...\n", MQTT_BROKER, MQTT_PORT);
  if (mqttClient.connect(MQTT_CLIENT_ESP32, MQTT_USER, MQTT_PASSWORD_BROKER)) {
    Serial.println("[MQTT] Connected");
    mqttClient.subscribe(TOPIC_FATIGUE);
    mqttClient.subscribe(TOPIC_CMD_THRESHOLD);
    mqttClient.subscribe(TOPIC_CMD_CONTACT);
    mqttClient.subscribe(TOPIC_CMD_RESET);
  } else {
    Serial.printf("[MQTT] Failed, rc=%d\n", mqttClient.state());
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int len) {
  char msg[len + 1];
  memcpy(msg, payload, len);
  msg[len] = '\0';

  if (strcmp(topic, TOPIC_FATIGUE) == 0) {
    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, msg) == DeserializationError::Ok) {
      bool newFatigue = doc["fatigue"] | false;
      if (newFatigue && !g_fatigue) {
        g_fatigue = true;
        triggerAlert("fatigue", "Driver fatigue detected");
      } else if (!newFatigue) {
        g_fatigue = false;
      }
    }
  } else if (strcmp(topic, TOPIC_CMD_RESET) == 0) {
    g_crashDetected  = false;
    g_alcoholAlert   = false;
    g_heartRateAlert = false;
    g_leanAngleAlert = false;
    g_fatigue        = false;
    Serial.println("[MAIN] Alerts reset via MQTT");
  }
}

// ===========================================================================
// Sensor Reads
// ===========================================================================
void readGPS() {
  if (gps.location.isValid()) {
    g_latitude      = (float)gps.location.lat();
    g_longitude     = (float)gps.location.lng();
    g_altitude_gps  = (float)gps.altitude.meters();
  }
}

void readHeartRate() {
  long irValue = heartSensor.getIR();
  if (checkForBeat(irValue)) {
    long delta = millis() - lastBeat;
    lastBeat   = millis();
    beatsPerMinute = 60.0f / (delta / 1000.0f);
    if (beatsPerMinute > 20 && beatsPerMinute < 255) {
      rates[rateSpot++ % RATE_SIZE] = (byte)beatsPerMinute;
      int sum = 0;
      for (byte i = 0; i < RATE_SIZE; i++) sum += rates[i];
      beatAvg = sum / RATE_SIZE;
    }
  }
  g_heartRate = beatAvg;

  // Simple SpO2 estimate (red/IR ratio) – rough approximation
  long redValue = heartSensor.getRed();
  if (irValue > 50000 && redValue > 0) {
    float ratio = (float)redValue / (float)irValue;
    g_spo2 = (int)(110.0f - 25.0f * ratio);
    g_spo2 = constrain(g_spo2, 0, 100);
  }
}

void readGyroscope() {
  if (mpu.update()) {
    g_accelX    = mpu.getAccX();
    g_accelY    = mpu.getAccY();
    g_accelZ    = mpu.getAccZ();
    g_leanAngle = mpu.getRoll();

    // Crash detection: sudden large acceleration change
    float totalAccel = sqrt(g_accelX * g_accelX + g_accelY * g_accelY + g_accelZ * g_accelZ);
    // Subtract gravity component (≈1 g) and check spike
    float accelDelta = abs(totalAccel - 1.0f);
    if (accelDelta > CRASH_ACCEL_THRESHOLD) {
      if (!g_crashDetected) {
        g_crashDetected = true;
        triggerAlert("crash", "CRASH DETECTED – emergency alert");
      }
    }
  }
}

void readAlcohol() {
  g_alcoholRaw = analogRead(ALCOHOL_PIN);
  // Map raw ADC (0-4095) to 0-100 %
  g_alcoholPct = map(g_alcoholRaw, 0, 4095, 0, 100);
}

void readPressure() {
  g_pressureRaw = analogRead(PRESSURE_PIN);
  // Rough altitude estimate from analog pressure sensor output
  // Assumes sensor maps 0–4095 to 0–5 V, with sea level at ~4.2 V
  float voltage = (g_pressureRaw / 4095.0f) * 3.3f;
  // Barometric formula approximation (sensor-dependent – calibrate on site)
  g_altitude_bar = (1.0f - pow(voltage / 3.3f, 5.2561f)) * 44330.0f;
}

// ===========================================================================
// Alert Logic
// ===========================================================================
void checkAlerts() {
  // Heart rate
  if (g_heartRate > 0) {
    bool hrHigh = (g_heartRate > HEART_RATE_MAX);
    bool hrLow  = (g_heartRate < HEART_RATE_MIN);
    if ((hrHigh || hrLow) && !g_heartRateAlert) {
      g_heartRateAlert = true;
      char msg[64];
      snprintf(msg, sizeof(msg), "Abnormal heart rate: %d BPM", g_heartRate);
      triggerAlert("heart_rate", msg);
    } else if (!hrHigh && !hrLow) {
      g_heartRateAlert = false;
    }
  }

  // Alcohol
  if (g_alcoholRaw > ALCOHOL_THRESHOLD && !g_alcoholAlert) {
    g_alcoholAlert = true;
    char msg[64];
    snprintf(msg, sizeof(msg), "High alcohol detected: %.0f%%", g_alcoholPct);
    triggerAlert("alcohol", msg);
  } else if (g_alcoholRaw <= ALCOHOL_THRESHOLD) {
    g_alcoholAlert = false;
  }

  // Lean angle
  if (abs(g_leanAngle) > LEAN_ANGLE_MAX && !g_leanAngleAlert) {
    g_leanAngleAlert = true;
    char msg[64];
    snprintf(msg, sizeof(msg), "Extreme lean angle: %.1f deg", g_leanAngle);
    triggerAlert("lean_angle", msg);
  } else if (abs(g_leanAngle) <= LEAN_ANGLE_MAX) {
    g_leanAngleAlert = false;
  }
}

void triggerAlert(const char* alertType, const char* message) {
  Serial.printf("[ALERT] ⚠️  %s – %s\n", alertType, message);
  buzzAlert(3);
  digitalWrite(LED_PIN, HIGH);
  delay(500);
  digitalWrite(LED_PIN, LOW);

  // Publish alert over MQTT
  StaticJsonDocument<256> doc;
  doc["type"]      = alertType;
  doc["message"]   = message;
  doc["lat"]       = g_latitude;
  doc["lon"]       = g_longitude;
  doc["timestamp"] = millis();
  char buf[256];
  serializeJson(doc, buf);
  mqttClient.publish(TOPIC_ALERT, buf, true);

  // SMS alert (rate-limited)
  unsigned long now = millis();
  if (now - g_lastSmsSent > SMS_COOLDOWN_MS) {
    g_lastSmsSent = now;
    String smsText = buildSMSMessage(alertType);
    sendSMS(EMERGENCY_CONTACT_1, smsText.c_str());
    sendSMS(EMERGENCY_CONTACT_2, smsText.c_str());
  }
}

String buildSMSMessage(const char* alertType) {
  String msg = "SMART HELMET ALERT!\n";
  msg += "Type: ";
  msg += alertType;
  msg += "\n";
  if (g_latitude != 0.0f || g_longitude != 0.0f) {
    msg += "GPS: https://maps.google.com/?q=";
    msg += String(g_latitude, 6);
    msg += ",";
    msg += String(g_longitude, 6);
    msg += "\n";
  } else {
    msg += "GPS: not available\n";
  }
  msg += "HR: ";
  msg += g_heartRate;
  msg += " BPM, SpO2: ";
  msg += g_spo2;
  msg += "%\n";
  msg += "Alcohol: ";
  msg += String(g_alcoholPct, 0);
  msg += "%\n";
  msg += "Lean: ";
  msg += String(g_leanAngle, 1);
  msg += " deg";
  return msg;
}

void sendSMS(const char* number, const char* message) {
  if (strlen(number) < 6) return;   // Skip placeholder numbers
  Serial.printf("[SMS] Sending to %s\n", number);
  sim800Serial.print("AT+CMGS=\"");
  sim800Serial.print(number);
  sim800Serial.println("\"");
  delay(500);
  sim800Serial.print(message);
  delay(100);
  sim800Serial.write(26);  // Ctrl+Z to send
  delay(2000);
  Serial.println("[SMS] Sent");
}

void buzzAlert(int count) {
  for (int i = 0; i < count; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(200);
    digitalWrite(BUZZER_PIN, LOW);
    delay(200);
  }
}

// ===========================================================================
// MQTT Publish – all sensor data as JSON
// ===========================================================================
void publishSensorData() {
  if (!mqttClient.connected()) return;

  // GPS
  StaticJsonDocument<128> gpsDoc;
  gpsDoc["lat"] = g_latitude;
  gpsDoc["lon"] = g_longitude;
  gpsDoc["alt"] = g_altitude_gps;
  char gpsBuf[128];
  serializeJson(gpsDoc, gpsBuf);
  mqttClient.publish(TOPIC_GPS, gpsBuf);

  // Heart rate
  char hrBuf[16];
  snprintf(hrBuf, sizeof(hrBuf), "%d", g_heartRate);
  mqttClient.publish(TOPIC_HEART_RATE, hrBuf);

  // SpO2
  char spo2Buf[16];
  snprintf(spo2Buf, sizeof(spo2Buf), "%d", g_spo2);
  mqttClient.publish(TOPIC_SPO2, spo2Buf);

  // Alcohol
  char alcBuf[16];
  snprintf(alcBuf, sizeof(alcBuf), "%.1f", g_alcoholPct);
  mqttClient.publish(TOPIC_ALCOHOL, alcBuf);

  // Lean angle
  char leanBuf[16];
  snprintf(leanBuf, sizeof(leanBuf), "%.1f", g_leanAngle);
  mqttClient.publish(TOPIC_LEAN_ANGLE, leanBuf);

  // Pressure / altitude
  char altBuf[16];
  snprintf(altBuf, sizeof(altBuf), "%.1f", g_altitude_bar);
  mqttClient.publish(TOPIC_ALTITUDE, altBuf);

  // Crash flag
  mqttClient.publish(TOPIC_CRASH, g_crashDetected ? "1" : "0");
}
