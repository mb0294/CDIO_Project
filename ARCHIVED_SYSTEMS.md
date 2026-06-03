# Archived / Deprecated Systems

## MQTT system (deprecated)
The MQTT-based architecture is archived as a legacy path.

### Archived scope
- `firmwares/mqtt_system/*`
- `servers/mqtt_server.py`
- `requirements/requirements_mqtt.txt`
- `servers/mqtt_dashboard.html`

### Reason for retirement
- Higher setup complexity (broker + multiple microcontrollers)
- Eye-detection path is disabled in MQTT server due to MediaPipe incompatibility
- HTTP architecture provides a simpler and reliable end-to-end detection flow

### Current recommendation
Use the HTTP stack:
- `firmwares/http_system/eye.ino`
- `servers/http_detector.py`
- `requirements/requirements_clean.txt`

For migration instructions, see `MIGRATION_GUIDE.md`.
