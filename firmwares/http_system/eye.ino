/*
 * ESP32-CAM Drowsiness Detector Firmware
 * TheInfoFlux — YouTube Project
 *
 * Features:
 *  - MJPEG HTTP stream on /stream        (port 80)
 *  - GPIO LED control via /buzzer     (port 81)
 *  - Status endpoint on /status          (port 81)
 *
 * Board : AI Thinker ESP32-CAM
 * Buzzer: Active LED on GPIO 14
 *
 * Libraries needed:
 *   - ESP32 board package (espressif/arduino-esp32 v2.x)
 */

#include "esp_camera.h"
#include <WiFi.h>
#include "esp_http_server.h"

// ─── WiFi credentials ──────────────────────────────────────────────────────
const char* WIFI_SSID = "MB";
const char* WIFI_PASS = "00000001";

// ─── LED ────────────────────────────────────────────────────────────────
#define LED_PIN 14

// ─── Camera pin map — AI Thinker ESP32-CAM ─────────────────────────────────
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

// ─── Globals ───────────────────────────────────────────────────────────────
httpd_handle_t stream_httpd = NULL;
httpd_handle_t cmd_httpd    = NULL;
bool LEDActive = false;

// ─── MJPEG stream handler ──────────────────────────────────────────────────
#define PART_BOUNDARY "123456789000000000000987654321"
static const char* STREAM_CONTENT_TYPE =
  "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* STREAM_BOUNDARY =
  "\r\n--" PART_BOUNDARY "\r\n";
static const char* STREAM_PART =
  "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

esp_err_t stream_handler(httpd_req_t* req) {
  camera_fb_t* fb = NULL;
  esp_err_t res = ESP_OK;
  char part_buf[64];

  res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
  if (res != ESP_OK) return res;

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      res = ESP_FAIL;
      break;
    }

    res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
    if (res != ESP_OK) { esp_camera_fb_return(fb); break; }

    size_t hlen = snprintf(part_buf, sizeof(part_buf), STREAM_PART, fb->len);
    res = httpd_resp_send_chunk(req, part_buf, hlen);
    if (res != ESP_OK) { esp_camera_fb_return(fb); break; }

    res = httpd_resp_send_chunk(req, (const char*)fb->buf, fb->len);
    esp_camera_fb_return(fb);
    if (res != ESP_OK) break;
  }
  return res;
}

// ─── LED control handler — port 81 (/LED?state=on|off) ───────────────
esp_err_t LED_handler(httpd_req_t* req) {
  char buf[32];
  size_t buf_len = httpd_req_get_url_query_len(req) + 1;

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  if (buf_len > 1 && buf_len <= sizeof(buf)) {
    if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
      char val[8];
      if (httpd_query_key_value(buf, "state", val, sizeof(val)) == ESP_OK) {
        if (strcmp(val, "on") == 0) {
          digitalWrite(LED_PIN, HIGH);
          LEDActive = true;
        } else {
          digitalWrite(LED_PIN, LOW);
          LEDActive = false;
        }
      }
    }
  }

  const char* resp = LEDActive ? "{\"LED\":\"on\"}" : "{\"LED\":\"off\"}";
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, resp);
  return ESP_OK;
}

// ─── Status handler — port 81 (/status) ────────────────────────────────────
esp_err_t status_handler(httpd_req_t* req) {
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_type(req, "application/json");
  String json = "{\"ip\":\"" + WiFi.localIP().toString() +
                "\",\"LED\":\"" + (LEDActive ? "on" : "off") + "\"}";
  httpd_resp_sendstr(req, json.c_str());
  return ESP_OK;
}

// ─── Start both servers ────────────────────────────────────────────────────
void startStreamServer() {
  // ── Stream server on port 80 (stream only) ──
  httpd_config_t stream_config = HTTPD_DEFAULT_CONFIG();
  stream_config.server_port = 80;
  stream_config.ctrl_port   = 32768;

  httpd_uri_t stream_uri = { "/stream", HTTP_GET, stream_handler, NULL };

  if (httpd_start(&stream_httpd, &stream_config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &stream_uri);
    Serial.println("Stream server started on port 80");
  } else {
    Serial.println("Stream server FAILED to start!");
  }

  // ── Command server on port 81 (LED + status) ──
  httpd_config_t cmd_config = HTTPD_DEFAULT_CONFIG();
  cmd_config.server_port = 81;
  cmd_config.ctrl_port   = 32769;

  httpd_uri_t LED_uri = { "/LED", HTTP_GET, LED_handler, NULL };
  httpd_uri_t status_uri = { "/status", HTTP_GET, status_handler, NULL };

  if (httpd_start(&cmd_httpd, &cmd_config) == ESP_OK) {
    httpd_register_uri_handler(cmd_httpd, &LED_uri);
    httpd_register_uri_handler(cmd_httpd, &status_uri);
    Serial.println("Command server started on port 81");
  } else {
    Serial.println("Command server FAILED to start!");
  }
}

// ─── Camera init ───────────────────────────────────────────────────────────
bool initCamera() {
  camera_config_t config;
  config.ledc_channel  = LEDC_CHANNEL_0;
  config.ledc_timer    = LEDC_TIMER_0;
  config.pin_d0        = Y2_GPIO_NUM;
  config.pin_d1        = Y3_GPIO_NUM;
  config.pin_d2        = Y4_GPIO_NUM;
  config.pin_d3        = Y5_GPIO_NUM;
  config.pin_d4        = Y6_GPIO_NUM;
  config.pin_d5        = Y7_GPIO_NUM;
  config.pin_d6        = Y8_GPIO_NUM;
  config.pin_d7        = Y9_GPIO_NUM;
  config.pin_xclk      = XCLK_GPIO_NUM;
  config.pin_pclk      = PCLK_GPIO_NUM;
  config.pin_vsync     = VSYNC_GPIO_NUM;
  config.pin_href      = HREF_GPIO_NUM;
  config.pin_sccb_sda  = SIOD_GPIO_NUM;
  config.pin_sccb_scl  = SIOC_GPIO_NUM;
  config.pin_pwdn      = PWDN_GPIO_NUM;
  config.pin_reset      = RESET_GPIO_NUM;
  config.xclk_freq_hz  = 20000000;
  config.pixel_format  = PIXFORMAT_JPEG;
  config.grab_mode     = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location   = CAMERA_FB_IN_PSRAM;

  if (psramFound()) {
    config.frame_size   = FRAMESIZE_VGA;
    config.jpeg_quality = 12;
    config.fb_count     = 2;
  } else {
    config.frame_size   = FRAMESIZE_QVGA;
    config.jpeg_quality = 15;
    config.fb_count     = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    return false;
  }

  sensor_t* s = esp_camera_sensor_get();
  s->set_brightness(s, 1);
  s->set_contrast(s, 1);
  s->set_saturation(s, 0);
  s->set_sharpness(s, 1);
  s->set_denoise(s, 1);
  s->set_whitebal(s, 1);
  s->set_awb_gain(s, 1);
  s->set_exposure_ctrl(s, 1);
  s->set_aec2(s, 1);
  s->set_gain_ctrl(s, 1);
  s->set_agc_gain(s, 0);
  s->set_gainceiling(s, (gainceiling_t)2);
  s->set_bpc(s, 0);
  s->set_wpc(s, 1);
  s->set_raw_gma(s, 1);
  s->set_lenc(s, 1);
  s->set_hmirror(s, 0);
  s->set_vflip(s, 0);
  s->set_dcw(s, 1);
  s->set_colorbar(s, 0);

  return true;
}

// ─── Setup ─────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  if (!initCamera()) {
    Serial.println("Camera init failed! Halting.");
    while (true) delay(1000);
  }
  Serial.println("Camera ready.");

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected! IP: ");
  Serial.println(WiFi.localIP());
  Serial.println("Endpoints:");
  Serial.print("  Stream : http://"); Serial.print(WiFi.localIP()); Serial.println("/stream");
  Serial.print("  LED : http://"); Serial.print(WiFi.localIP()); Serial.println(":81/LED?state=on");
  Serial.print("  Status : http://"); Serial.print(WiFi.localIP()); Serial.println(":81/status");

  // Startup beep
  digitalWrite(LED_PIN, HIGH);
  delay(200);
  digitalWrite(LED_PIN, LOW);

  startStreamServer();
}

void loop() {
  delay(10000);
}