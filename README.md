# 🏍️ Smart Helmet System

A comprehensive safety system for motorcycle riders combining real-time fatigue
detection, health monitoring, crash detection, and automatic emergency alerts.

---

## 📦 Project Structure

```
CDIO_Project/
├── esp32_cam_eye_detection/
│   └── esp32_cam_eye_detection.ino   # ESP32-CAM – video stream + eye detection
├── main_esp32_controller/
│   └── main_esp32_controller.ino     # Main ESP32 – all sensors + alerts + SMS
├── test/                             # Individual sensor test sketches
│   ├── GPS_NEO-6M/
│   ├── MAX30102_pulse/
│   ├── MPU-9250_Gyro/
│   ├── SIM800L_SMS_test/
│   ├── mq3_acoooool/
│   ├── HC-SR04-Ultrason/
│   └── Capteur_pression/
├── mqtt_config.h                     # MQTT broker settings & topic definitions
├── sensor_config.h                   # Pin definitions & alert thresholds
├── dashboard.html                    # Web dashboard (open in browser)
├── server.py                         # Flask + Socket.io backend + MQTT bridge
├── requirements.txt                  # Python dependencies
└── README.md
```

---

## 🖥️ Hardware

| Component | Module | Interface |
|-----------|--------|-----------|
| Camera + Eye Detection | ESP32-CAM (AI-Thinker) | WiFi |
| Microcontroller | ESP32 (30-pin) | – |
| GPS | NEO-6M | UART1 (RX=16, TX=17) |
| Heart Rate + SpO2 | MAX30102 | I2C (SDA=21, SCL=22) |
| Gyroscope / IMU | MPU-9250 | I2C (addr 0x68) |
| Alcohol | MQ-3 | ADC (pin 34) |
| Pressure | Analog sensor | ADC (pin 35) |
| GSM / SMS | SIM800L | UART2 (RX=26, TX=27) |
| Buzzer | Active buzzer | GPIO 25 |
| LED | Built-in / external | GPIO 2 |

---

## ⚡ Wiring Diagram (Main ESP32)

```
ESP32 Pin  →  Module
─────────────────────────────────────────────
3.3 V      →  MAX30102 VCC, MPU-9250 VCC
GND        →  All GND
GPIO 21    →  I2C SDA (MAX30102 + MPU-9250)
GPIO 22    →  I2C SCL (MAX30102 + MPU-9250)
GPIO 16    →  GPS RX (NEO-6M TX)
GPIO 17    →  GPS TX (NEO-6M RX)
GPIO 26    →  SIM800L RX
GPIO 27    →  SIM800L TX
GPIO 34    →  MQ-3 AOUT
GPIO 35    →  Pressure sensor AOUT
GPIO 25    →  Buzzer +
GPIO 2     →  LED +
```

> ⚠️ **SIM800L requires 3.7–4.2 V / 2 A peak**. Use a dedicated LiPo or power
> regulator. Do **not** power it from the ESP32 3.3 V rail.

---

## 🔧 Arduino Setup

### 1. Install Board Package
In Arduino IDE → Preferences → Additional Board URLs:
```
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```
Then install **esp32 by Espressif Systems** from Boards Manager.

### 2. Install Libraries (Library Manager)
| Library | Author |
|---------|--------|
| TinyGPSPlus | Mikal Hart |
| SparkFun MAX3010x Pulse and Proximity Sensor Library | SparkFun |
| MPU9250 | hideakitai |
| PubSubClient | Nick O'Leary |
| ArduinoJson | Benoit Blanchon |

### 3. Configure WiFi & Contacts
Edit `mqtt_config.h`:
```c
#define WIFI_SSID     "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
```

Edit `sensor_config.h`:
```c
#define EMERGENCY_CONTACT_1  "+33600000000"
#define EMERGENCY_CONTACT_2  "+33600000001"
```

### 4. Flash ESP32-CAM
- Board: **AI Thinker ESP32-CAM**
- Open `esp32_cam_eye_detection/esp32_cam_eye_detection.ino`
- Upload. Note the IP address printed on Serial Monitor.

### 5. Flash Main ESP32
- Board: **ESP32 Dev Module**
- Open `main_esp32_controller/main_esp32_controller.ino`
- Upload.

---

## 🐍 Python Server Setup

### Requirements
- Python 3.9+
- A machine on the **same WiFi** as the ESP32 boards

### Install dependencies
```bash
pip install -r requirements.txt
```

### Set environment variables (optional)
```bash
export MQTT_BROKER=broker.hivemq.com   # or your local Mosquitto IP
export CAM_STREAM_URL=http://192.168.1.42/stream   # ESP32-CAM IP
export FLASK_PORT=5000
```

### Run the server
```bash
python server.py
```

Open `http://localhost:5000` (or your machine's IP) in a browser to see the
dashboard.

---

## 🌐 MQTT Broker Options

### Option A – HiveMQ Public Broker (default, no setup)
```c
#define MQTT_BROKER "broker.hivemq.com"
#define MQTT_PORT   1883
```

### Option B – Local Mosquitto (recommended for production)
```bash
sudo apt install mosquitto mosquitto-clients
sudo systemctl start mosquitto
```
Then set `MQTT_BROKER` to the machine's local IP (e.g. `192.168.1.10`).

---

## 📊 MQTT Topics Reference

| Topic | Publisher | Payload |
|-------|-----------|---------|
| `helmet/sensors/gps` | ESP32 | `{"lat":48.85,"lon":2.35,"alt":35.0}` |
| `helmet/sensors/heart_rate` | ESP32 | `75` |
| `helmet/sensors/spo2` | ESP32 | `98` |
| `helmet/sensors/alcohol` | ESP32 | `12.5` |
| `helmet/sensors/lean_angle` | ESP32 | `-23.4` |
| `helmet/sensors/altitude` | ESP32 | `120.0` |
| `helmet/sensors/crash` | ESP32 | `0` or `1` |
| `helmet/eye/ear` | ESP32-CAM / Server | `{"ear_avg":0.28,"fatigue":false,...}` |
| `helmet/eye/blink_rate` | ESP32-CAM / Server | `15` |
| `helmet/eye/fatigue` | ESP32-CAM / Server | `{"fatigue":true,"ear":0.12,...}` |
| `helmet/alert` | Both | `{"type":"crash","lat":...,"lon":...}` |
| `helmet/cmd/threshold` | Server / Dashboard | `{"hr_max":120,"alcohol":50,...}` |
| `helmet/cmd/reset` | Server / Dashboard | `reset` |

---

## 🚨 Alert Thresholds (defaults)

| Condition | Threshold | Action |
|-----------|-----------|--------|
| High heart rate | > 120 BPM | Buzzer + SMS + MQTT alert |
| Low heart rate | < 50 BPM | Buzzer + SMS + MQTT alert |
| Low SpO₂ | < 90 % | MQTT alert |
| Alcohol | ADC > 2000 (~49 %) | Buzzer + SMS + MQTT alert |
| Lean angle | > 45° | Buzzer + MQTT alert |
| Crash (accel spike) | Δg > 3 g | Buzzer + SMS (immediate) |
| Eye closed | > 2 s or 20 frames | Buzzer + SMS + MQTT alert |
| Low blink rate | < 10 blinks/min | Warning alert |

All thresholds can be changed live via the dashboard **Configuration** panel.

---

## 📱 Features

- **Live MJPEG video** stream from ESP32-CAM
- **Eye detection** (MediaPipe Face Mesh) running on the server
  - Eye Aspect Ratio (EAR) calculation
  - Prolonged eye closure detection
  - Blink rate monitoring
- **Real-time charts** – heart rate, alcohol level
- **GPS map** – live position via Leaflet / OpenStreetMap
- **Lean angle arc** gauge
- **Alert history** panel
- **SMS alerts** with Google Maps link to GPS location
- **Threshold & contact management** via dashboard

---

## ✅ Success Criteria

| Criterion | Status |
|-----------|--------|
| ESP32-CAM streams MJPEG video | ✅ |
| All sensors read & publish via MQTT | ✅ |
| Fatigue detection triggers alerts | ✅ |
| SMS alerts sent with GPS coordinates | ✅ |
| Web dashboard displays real-time data | ✅ |
| System designed for continuous operation | ✅ |
| Emergency contacts receive SMS within 5 s | ✅ (SIM800L AT+CMGS) |

---

## 🛠️ Troubleshooting

**ESP32-CAM won't connect to WiFi**
- Check SSID/password in `mqtt_config.h`
- Ensure the board is powered with ≥ 500 mA

**No GPS fix**
- Move outdoors, NEO-6M needs a clear sky view
- Cold start can take up to 2 minutes

**SIM800L not responding**
- Check baud rate (9600) and power supply (≥ 2 A)
- Try `AT` command manually via Serial Monitor

**Eye detection not working**
- Ensure `CAM_STREAM_URL` is set correctly
- Check that `mediapipe` is installed: `pip install mediapipe`
- Verify the stream URL is accessible: `curl http://<esp32-cam-ip>/stream`

**Dashboard shows no data**
- Confirm both ESP32s and the server are on the same network / MQTT broker
- Check MQTT broker address in `mqtt_config.h` and `server.py`

---

## 📅 Timeline

| Milestone | Target |
|-----------|--------|
| Hardware wiring + individual sensor tests | Week 1 day 1–2 |
| ESP32-CAM stream + MQTT integration | Week 1 day 2–3 |
| Main ESP32 full sensor loop + alerts | Week 1 day 3–4 |
| Python server + eye detection pipeline | Week 1 day 4–5 |
| Dashboard UI + end-to-end testing | Week 1 day 5–7 |
| **Deadline** | **2026-05-06** |

---

## 📜 License

This project is developed for academic purposes (CDIO Project).
