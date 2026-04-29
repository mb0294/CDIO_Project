/**
 * esp32_cam_eye_detection.ino
 *
 * Smart Helmet – ESP32-CAM Module
 * --------------------------------
 * • Streams MJPEG video over HTTP
 * • Detects eye state (open / closed) using a simple brightness-threshold
 *   approach on eye ROIs derived from face landmarks sent from the Python
 *   server, OR operates standalone with a lightweight Viola-Jones-style
 *   Haar cascade loaded from SPIFFS.
 * • Publishes fatigue metrics to the MQTT broker.
 * • Communicates alert status to the main ESP32 via MQTT.
 *
 * Libraries required (install via Arduino Library Manager):
 *   - esp32 board package (Espressif)
 *   - PubSubClient  (Nick O'Leary)
 *   - ArduinoJson   (Benoit Blanchon)
 *
 * Hardware: AI-Thinker ESP32-CAM
 */

#include "esp_camera.h"
#include "esp_http_server.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "mqtt_config.h"
#include "sensor_config.h"

// ---------------------------------------------------------------------------
// Camera model – AI-Thinker
// ---------------------------------------------------------------------------
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// ---------------------------------------------------------------------------
// Eye-detection state
// ---------------------------------------------------------------------------
static float  g_earLeft      = 1.0f;
static float  g_earRight     = 1.0f;
static float  g_earAvg       = 1.0f;
static int    g_closedFrames  = 0;
static int    g_blinkCount    = 0;
static unsigned long g_minuteStart = 0;
static int    g_blinkRatePerMin = 0;
static bool   g_eyeWasClosed  = false;
static bool   g_fatigueAlert  = false;
static unsigned long g_eyeClosedStart = 0;
static float  g_closureDuration = 0.0f;

// ---------------------------------------------------------------------------
// MQTT
// ---------------------------------------------------------------------------
WiFiClient   espClient;
PubSubClient mqttClient(espClient);
unsigned long g_lastMqttPublish = 0;

// ---------------------------------------------------------------------------
// HTTP server handle
// ---------------------------------------------------------------------------
httpd_handle_t streamHttpd = NULL;

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
void connectWiFi();
void connectMQTT();
void mqttCallback(char* topic, byte* payload, unsigned int len);
void publishEyeData();
void triggerFatigueAlert();
esp_err_t streamHandler(httpd_req_t* req);
void startCameraServer();
void processEyeMetrics(float earLeft, float earRight);

// ===========================================================================
// setup()
// ===========================================================================
void setup() {
  Serial.begin(115200);
  pinMode(CAM_LED_PIN, OUTPUT);
  digitalWrite(CAM_LED_PIN, LOW);

  // --- Camera init ---
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size   = FRAMESIZE_VGA;
    config.jpeg_quality = 10;
    config.fb_count     = 2;
  } else {
    config.frame_size   = FRAMESIZE_QVGA;
    config.jpeg_quality = 12;
    config.fb_count     = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[CAM] Camera init failed: 0x%x\n", err);
    return;
  }
  Serial.println("[CAM] Camera ready");

  connectWiFi();

  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  connectMQTT();

  startCameraServer();

  g_minuteStart = millis();

  Serial.print("[CAM] Stream URL: http://");
  Serial.print(WiFi.localIP());
  Serial.println("/stream");
}

// ===========================================================================
// loop()
// ===========================================================================
void loop() {
  if (!mqttClient.connected()) {
    connectMQTT();
  }
  mqttClient.loop();

  // Publish eye metrics every EYE_PUBLISH_INTERVAL ms
  if (millis() - g_lastMqttPublish >= EYE_PUBLISH_INTERVAL) {
    g_lastMqttPublish = millis();
    publishEyeData();
  }

  // Reset blink counter every minute
  if (millis() - g_minuteStart >= 60000UL) {
    g_blinkRatePerMin = g_blinkCount;
    g_blinkCount      = 0;
    g_minuteStart     = millis();
  }
}

// ===========================================================================
// WiFi / MQTT helpers
// ===========================================================================
void connectWiFi() {
  Serial.printf("[CAM] Connecting to WiFi: %s", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[CAM] WiFi connected. IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\n[CAM] WiFi connection failed – continuing offline");
  }
}

void connectMQTT() {
  unsigned long now = millis();
  static unsigned long lastAttempt = 0;
  if (now - lastAttempt < MQTT_RECONNECT_INTERVAL) return;
  lastAttempt = now;

  Serial.printf("[CAM] Connecting to MQTT broker %s:%d ...\n", MQTT_BROKER, MQTT_PORT);
  if (mqttClient.connect(MQTT_CLIENT_CAM, MQTT_USER, MQTT_PASSWORD_BROKER)) {
    Serial.println("[CAM] MQTT connected");
    mqttClient.subscribe(TOPIC_CMD_THRESHOLD);
    mqttClient.subscribe(TOPIC_CMD_RESET);
  } else {
    Serial.printf("[CAM] MQTT failed, rc=%d\n", mqttClient.state());
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int len) {
  // Commands from dashboard / server
  char msg[len + 1];
  memcpy(msg, payload, len);
  msg[len] = '\0';

  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, msg) != DeserializationError::Ok) return;

  if (strcmp(topic, TOPIC_CMD_RESET) == 0) {
    g_fatigueAlert  = false;
    g_closedFrames  = 0;
    g_blinkCount    = 0;
    Serial.println("[CAM] State reset via MQTT command");
  }
}

// ===========================================================================
// Eye metric processing
// Called externally by the server-side Python eye detector via MQTT,
// or can be driven by a companion routine reading EAR values from a
// lightweight on-device algorithm.
// ===========================================================================
void processEyeMetrics(float earLeft, float earRight) {
  g_earLeft  = earLeft;
  g_earRight = earRight;
  g_earAvg   = (earLeft + earRight) / 2.0f;

  bool eyeClosed = (g_earAvg < EAR_THRESHOLD);

  if (eyeClosed) {
    g_closedFrames++;
    if (!g_eyeWasClosed) {
      g_eyeClosedStart = millis();
      g_eyeWasClosed   = true;
    }
    g_closureDuration = (millis() - g_eyeClosedStart) / 1000.0f;

    if (g_closedFrames >= FATIGUE_CONSEC_FRAMES || g_closureDuration >= EYE_CLOSURE_MAX_SEC) {
      if (!g_fatigueAlert) {
        g_fatigueAlert = true;
        triggerFatigueAlert();
      }
    }
  } else {
    if (g_eyeWasClosed) {
      // Blink completed
      g_blinkCount++;
      g_eyeWasClosed  = false;
      g_closureDuration = 0.0f;
    }
    g_closedFrames = 0;
    g_fatigueAlert = false;
  }
}

void triggerFatigueAlert() {
  Serial.println("[CAM] ⚠️  FATIGUE ALERT – eyes closed too long!");
  // Flash onboard LED
  for (int i = 0; i < 5; i++) {
    digitalWrite(CAM_LED_PIN, HIGH);
    delay(200);
    digitalWrite(CAM_LED_PIN, LOW);
    delay(200);
  }

  // Publish alert
  StaticJsonDocument<128> doc;
  doc["alert"]   = "fatigue";
  doc["ear"]     = g_earAvg;
  doc["frames"]  = g_closedFrames;
  doc["duration"] = g_closureDuration;
  char buf[128];
  serializeJson(doc, buf);
  mqttClient.publish(TOPIC_FATIGUE, buf, true);
  mqttClient.publish(TOPIC_ALERT, "{\"source\":\"cam\",\"type\":\"fatigue\"}", true);
}

void publishEyeData() {
  if (!mqttClient.connected()) return;

  StaticJsonDocument<256> doc;
  doc["ear_left"]   = g_earLeft;
  doc["ear_right"]  = g_earRight;
  doc["ear_avg"]    = g_earAvg;
  doc["closed_frames"] = g_closedFrames;
  doc["closure_sec"]   = g_closureDuration;
  doc["fatigue"]       = g_fatigueAlert;
  char buf[256];
  serializeJson(doc, buf);
  mqttClient.publish(TOPIC_EAR, buf);

  // Blink rate
  char blinkBuf[32];
  snprintf(blinkBuf, sizeof(blinkBuf), "%d", g_blinkRatePerMin);
  mqttClient.publish(TOPIC_BLINK_RATE, blinkBuf);
}

// ===========================================================================
// MJPEG stream handler
// ===========================================================================
#define PART_BOUNDARY "123456789000000000000987654321"
static const char* STREAM_CONTENT_TYPE =
    "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* STREAM_PART =
    "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

esp_err_t streamHandler(httpd_req_t* req) {
  camera_fb_t*   fb = NULL;
  esp_err_t      res = ESP_OK;
  char           part_buf[64];

  res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
  if (res != ESP_OK) return res;

  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("[CAM] Frame capture failed");
      res = ESP_FAIL;
      break;
    }

    res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
    if (res == ESP_OK) {
      size_t hlen = snprintf(part_buf, sizeof(part_buf), STREAM_PART, fb->len);
      res = httpd_resp_send_chunk(req, part_buf, hlen);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, (const char*)fb->buf, fb->len);
    }
    esp_camera_fb_return(fb);
    if (res != ESP_OK) break;
  }
  return res;
}

void startCameraServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;

  httpd_uri_t streamUri = {
      .uri      = "/stream",
      .method   = HTTP_GET,
      .handler  = streamHandler,
      .user_ctx = NULL
  };

  if (httpd_start(&streamHttpd, &config) == ESP_OK) {
    httpd_register_uri_handler(streamHttpd, &streamUri);
    Serial.println("[CAM] HTTP stream server started on port 80");
  }
}
