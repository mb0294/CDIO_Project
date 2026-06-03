# MIGRATION GUIDE: MQTT → HTTP

## Why this migration
The MQTT architecture is no longer the recommended runtime path for drowsiness detection.

- HTTP mode provides direct ESP32-CAM streaming to MediaPipe
- Eye detection is fully functional in HTTP mode
- MQTT mode has known eye-detection incompatibility and is retained only for legacy sensor workflows

## What to use now
- **Python dependencies:** `requirements/requirements_clean.txt`
- **Main server:** `servers/http_detector.py`
- **Firmware:** `firmwares/http_system/eye.ino`

## Quick migration steps
1. Remove old virtual env dependencies (optional but recommended)
2. Install HTTP dependencies:
   ```bash
   pip install -r requirements/requirements_clean.txt
   ```
3. Flash `firmwares/http_system/eye.ino` to ESP32-CAM
4. Run:
   ```bash
   python servers/http_detector.py --esp32 http://<ESP32_IP>
   ```
5. Open dashboard at `http://localhost:5000`

## Mapping old MQTT flow to new HTTP flow
- `servers/mqtt_server.py` → `servers/http_detector.py`
- `requirements/requirements_mqtt.txt` → `requirements/requirements_clean.txt`
- Sensor-topic troubleshooting → stream URL and camera connectivity troubleshooting

## Troubleshooting after migration
- **No video:** verify `http://<ESP32_IP>/stream`
- **No detections:** reinstall `requirements_clean.txt`, verify camera quality and lighting
- **Dashboard not updating:** confirm detector process is running and URL is correct

## Legacy MQTT status
MQTT files remain in repository for archival/reference purposes and are marked deprecated.
