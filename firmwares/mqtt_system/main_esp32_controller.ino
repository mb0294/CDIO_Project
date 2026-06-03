/**
 * main_esp32_controller.ino
 *
 * Smart Helmet – Main ESP32 Sensor Controller
 * -------------------------------------------
 * Reads all helmet sensors and publishes data via HTTP.
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
 *   - ArduinoJson    (Benoit Blanchon)
 */

#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TinyGPS++.h>
#include <HardwareSerial.h>
#include "MAX30105.h"
#include "heartRate.h"
#include <MPU9250.h>
#include "http_config.h"
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
// Network
// ---------------------------------------------------------------------------
WiFiClient wifiClient;

// ---------------------------------------------------------------------------
// State / timing
// ---------------------------------------------------------------------------
unsigned long g_lastSensorRead     = 0;
unsigned long g_lastHttpPublish    = 0;
unsigned long g_lastCommandPoll    = 0;
unsigned long g_lastSmsSent        = 0;
unsigned long g_nextPublishAttempt = 0;
unsigned long g_nextPollAttempt    = 0;
unsigned long g_publishRetryDelay  = HTTP_RETRY_INITIAL_DELAY;
unsigned long g_pollRetryDelay     = HTTP_RETRY_INITIAL_DELAY;
unsigned int  g_publishRetries     = 0;
unsigned int  g_pollRetries        = 0;

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
bool    g_fatigue             = false;  // set via HTTP polling

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
bool httpPostJson(const char* path, const String& payload, int* code = nullptr, String* body = nullptr);
bool httpGet(const char* path, String& body, int* code = nullptr);
void readGPS();
void readHeartRate();
void readGyroscope();
void readAlcohol();
void readPressure();
void publishSensorData();
void checkCommandsFromServer();
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
  Serial.println("[MAIN] Smart Helmet controller ready ✓");
}

// ===========================================================================
// loop()
// ===========================================================================
void loop() {
  static unsigned long lastWifiAttempt = 0;
  unsigned long now = millis();

  if (WiFi.status() != WL_CONNECTED && now - lastWifiAttempt > 10000) {
    lastWifiAttempt = now;
    connectWiFi();
  }

  // Feed GPS decoder with available bytes
  while (gpsSerial.available()) {
    gps.encode(gpsSerial.read());
  }

  if (now - g_lastSensorRead >= SENSOR_READ_INTERVAL_MS) {
    g_lastSensorRead = now;
    readGPS();
    readHeartRate();
    readGyroscope();
    readAlcohol();
    readPressure();
    checkAlerts();
  }

  if (now - g_lastHttpPublish >= SENSOR_PUBLISH_INTERVAL && now >= g_nextPublishAttempt) {
    g_lastHttpPublish = now;
    publishSensorData();
  }

  if (now - g_lastCommandPoll >= COMMAND_POLL_INTERVAL && now >= g_nextPollAttempt) {
    g_lastCommandPoll = now;
    checkCommandsFromServer();
  }
}

// ===========================================================================
// WiFi / HTTP helpers
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

bool httpPostJson(const char* path, const String& payload, int* code, String* body) {
  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  String url = String(HTTP_SERVER_URL) + path;
  if (!http.begin(wifiClient, url)) {
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  if (strlen(HTTP_AUTH_TOKEN) > 0) {
    http.addHeader("Authorization", String("Bearer ") + HTTP_AUTH_TOKEN);
  }

  int httpCode = http.POST(payload);
  if (code) *code = httpCode;
  if (body && httpCode > 0) *body = http.getString();
  http.end();

  return httpCode >= 200 && httpCode < 300;
}

bool httpGet(const char* path, String& body, int* code) {
  body = "";
  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  String url = String(HTTP_SERVER_URL) + path;
  if (!http.begin(wifiClient, url)) {
    return false;
  }

  if (strlen(HTTP_AUTH_TOKEN) > 0) {
    http.addHeader("Authorization", String("Bearer ") + HTTP_AUTH_TOKEN);
  }

  int httpCode = http.GET();
  if (code) *code = httpCode;
  if (httpCode > 0) body = http.getString();
  http.end();

  return httpCode >= 200 && httpCode < 300;
}

void checkCommandsFromServer() {
  String payload;
  int httpCode = 0;
  if (!httpGet(API_COMMANDS_PENDING, payload, &httpCode)) {
    if (g_pollRetries < HTTP_MAX_RETRIES) g_pollRetries++;
    g_nextPollAttempt = millis() + g_pollRetryDelay;
    g_pollRetryDelay = min(g_pollRetryDelay * 2UL, (unsigned long)HTTP_RETRY_MAX_DELAY);
    Serial.printf("[HTTP] Command poll failed (code=%d), retry in %lu ms\n", httpCode, g_pollRetryDelay);
    return;
  }

  g_pollRetries = 0;
  g_pollRetryDelay = HTTP_RETRY_INITIAL_DELAY;
  g_nextPollAttempt = 0;

  StaticJsonDocument<512> doc;
  if (deserializeJson(doc, payload) != DeserializationError::Ok) {
    Serial.println("[HTTP] Command poll JSON parse error");
    return;
  }

  bool reset = doc["reset"] | false;
  if (reset) {
    g_crashDetected  = false;
    g_alcoholAlert   = false;
    g_heartRateAlert = false;
    g_leanAngleAlert = false;
    g_fatigue        = false;
    Serial.println("[MAIN] Alerts reset via HTTP command");
  }

  JsonVariant fatigueVar = doc["fatigue"];
  if (!fatigueVar.isNull()) {
    bool newFatigue = false;
    if (fatigueVar.is<bool>()) {
      newFatigue = fatigueVar.as<bool>();
    } else if (fatigueVar["value"].is<bool>()) {
      newFatigue = fatigueVar["value"].as<bool>();
    }

    if (newFatigue && !g_fatigue) {
      g_fatigue = true;
      triggerAlert("fatigue", "Driver fatigue detected");
    } else if (!newFatigue) {
      g_fatigue = false;
    }
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
  // Linear mapping of the raw ADC value (0–4095) to a 0–100 % scale.
  // NOTE: The MQ-3 response curve is logarithmic; this mapping is a
  // first-order approximation for relative comparison.  Calibrate the
  // ALCOHOL_THRESHOLD constant against known reference concentrations for
  // precise readings.
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

  // Publish alert over HTTP
  StaticJsonDocument<256> doc;
  doc["type"]      = alertType;
  doc["message"]   = message;
  doc["lat"]       = g_latitude;
  doc["lon"]       = g_longitude;
  doc["timestamp"] = millis();
  String payload;
  serializeJson(doc, payload);
  int code = 0;
  httpPostJson(API_ALERT, payload, &code, nullptr);

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
  // Skip numbers that are clearly placeholder values (length check rejects
  // strings like "+33600000000" that the user has not replaced yet, while
  // accepting real international numbers of 7+ digits including the + prefix).
  if (strlen(number) < 7) return;
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
// HTTP Publish – all sensor data as JSON batch
// ===========================================================================
void publishSensorData() {
  StaticJsonDocument<512> doc;
  JsonObject gpsDoc = doc.createNestedObject("gps");
  gpsDoc["lat"] = g_latitude;
  gpsDoc["lon"] = g_longitude;
  gpsDoc["alt"] = g_altitude_gps;

  doc["heart_rate"] = g_heartRate;
  doc["spo2"] = g_spo2;
  doc["alcohol"] = g_alcoholPct;
  doc["lean_angle"] = g_leanAngle;
  doc["pressure"] = g_pressureRaw;
  doc["altitude"] = g_altitude_bar;
  doc["crash"] = g_crashDetected;

  String payload;
  serializeJson(doc, payload);

  int httpCode = 0;
  if (httpPostJson(API_SENSORS_BATCH, payload, &httpCode, nullptr)) {
    g_publishRetries = 0;
    g_publishRetryDelay = HTTP_RETRY_INITIAL_DELAY;
    g_nextPublishAttempt = 0;
    return;
  }

  if (g_publishRetries < HTTP_MAX_RETRIES) g_publishRetries++;
  g_nextPublishAttempt = millis() + g_publishRetryDelay;
  g_publishRetryDelay = min(g_publishRetryDelay * 2UL, (unsigned long)HTTP_RETRY_MAX_DELAY);
  Serial.printf("[HTTP] Sensor publish failed (code=%d), retry in %lu ms\n", httpCode, g_publishRetryDelay);
}
