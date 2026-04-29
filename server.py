"""
server.py
Smart Helmet – Backend Server

Bridges MQTT sensor/eye data to a Socket.io web dashboard.
Also runs a MediaPipe eye-detection worker that pulls frames from the
ESP32-CAM stream and publishes EAR values back to the helmet.

Dependencies: see requirements.txt

Usage:
    python server.py

Environment variables (optional overrides):
    MQTT_BROKER        e.g. broker.hivemq.com
    MQTT_PORT          e.g. 1883
    CAM_STREAM_URL     e.g. http://192.168.1.42/stream
    FLASK_PORT         e.g. 5000
"""

import os
import json
import time
import threading
import math
import logging
from datetime import datetime

import cv2
import numpy as np
import mediapipe as mp
import paho.mqtt.client as mqtt
from flask import Flask, render_template, Response, send_from_directory
from flask_socketio import SocketIO

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
MQTT_BROKER   = os.getenv("MQTT_BROKER",    "broker.hivemq.com")
MQTT_PORT     = int(os.getenv("MQTT_PORT",  "1883"))
CAM_STREAM    = os.getenv("CAM_STREAM_URL", "http://192.168.1.42/stream")
FLASK_PORT    = int(os.getenv("FLASK_PORT", "5000"))

# MQTT topics (must match mqtt_config.h)
TOPIC_GPS          = "helmet/sensors/gps"
TOPIC_HEART_RATE   = "helmet/sensors/heart_rate"
TOPIC_SPO2         = "helmet/sensors/spo2"
TOPIC_ALCOHOL      = "helmet/sensors/alcohol"
TOPIC_LEAN_ANGLE   = "helmet/sensors/lean_angle"
TOPIC_PRESSURE     = "helmet/sensors/pressure"
TOPIC_ALTITUDE     = "helmet/sensors/altitude"
TOPIC_CRASH        = "helmet/sensors/crash"
TOPIC_BLINK_RATE   = "helmet/eye/blink_rate"
TOPIC_EAR          = "helmet/eye/ear"
TOPIC_FATIGUE      = "helmet/eye/fatigue"
TOPIC_ALERT        = "helmet/alert"
TOPIC_CMD_THRESHOLD = "helmet/cmd/threshold"
TOPIC_CMD_CONTACT   = "helmet/cmd/contact"
TOPIC_CMD_RESET     = "helmet/cmd/reset"

# Eye detection thresholds
EAR_THRESHOLD        = 0.20
EYE_CLOSURE_MAX_SEC  = 2.0
FATIGUE_CONSEC       = 20
BLINK_MIN_PER_MIN    = 10

# ---------------------------------------------------------------------------
# Logging
# ---------------------------------------------------------------------------
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
)
log = logging.getLogger(__name__)

# ---------------------------------------------------------------------------
# Shared state (thread-safe via a lock)
# ---------------------------------------------------------------------------
state_lock = threading.Lock()
state = {
    "gps":         {"lat": 0.0, "lon": 0.0, "alt": 0.0},
    "heart_rate":  0,
    "spo2":        0,
    "alcohol":     0.0,
    "lean_angle":  0.0,
    "altitude":    0.0,
    "crash":       False,
    "blink_rate":  0,
    "ear_avg":     1.0,
    "closure_sec": 0.0,
    "fatigue":     False,
    "alerts":      [],
    "last_updated": "",
}

# Trip history (in-memory, last 1000 entries)
trip_history = []
HISTORY_MAX = 1000

# ---------------------------------------------------------------------------
# Flask + SocketIO
# ---------------------------------------------------------------------------
app = Flask(__name__, static_folder=".", template_folder=".")
app.config["SECRET_KEY"] = "smart_helmet_secret"
socketio = SocketIO(app, cors_allowed_origins="*", async_mode="threading")

# ---------------------------------------------------------------------------
# MQTT client
# ---------------------------------------------------------------------------
mqtt_client = mqtt.Client(client_id="SmartHelmet_Server")


def _on_connect(client, userdata, flags, rc):
    if rc == 0:
        log.info("MQTT connected to %s:%d", MQTT_BROKER, MQTT_PORT)
        for topic in [
            TOPIC_GPS, TOPIC_HEART_RATE, TOPIC_SPO2, TOPIC_ALCOHOL,
            TOPIC_LEAN_ANGLE, TOPIC_ALTITUDE, TOPIC_CRASH,
            TOPIC_BLINK_RATE, TOPIC_EAR, TOPIC_FATIGUE, TOPIC_ALERT,
        ]:
            client.subscribe(topic)
    else:
        log.warning("MQTT connect failed, rc=%d", rc)


def _on_message(client, userdata, msg):
    topic   = msg.topic
    payload = msg.payload.decode("utf-8", errors="replace")

    with state_lock:
        try:
            if topic == TOPIC_GPS:
                data = json.loads(payload)
                state["gps"] = {
                    "lat": data.get("lat", 0.0),
                    "lon": data.get("lon", 0.0),
                    "alt": data.get("alt", 0.0),
                }
            elif topic == TOPIC_HEART_RATE:
                state["heart_rate"] = int(payload)
            elif topic == TOPIC_SPO2:
                state["spo2"] = int(payload)
            elif topic == TOPIC_ALCOHOL:
                state["alcohol"] = float(payload)
            elif topic == TOPIC_LEAN_ANGLE:
                state["lean_angle"] = float(payload)
            elif topic == TOPIC_ALTITUDE:
                state["altitude"] = float(payload)
            elif topic == TOPIC_CRASH:
                state["crash"] = (payload.strip() == "1")
            elif topic == TOPIC_BLINK_RATE:
                state["blink_rate"] = int(payload)
            elif topic == TOPIC_EAR:
                data = json.loads(payload)
                state["ear_avg"]     = data.get("ear_avg",     1.0)
                state["closure_sec"] = data.get("closure_sec", 0.0)
                state["fatigue"]     = data.get("fatigue",     False)
            elif topic == TOPIC_FATIGUE:
                data = json.loads(payload)
                state["fatigue"] = data.get("fatigue", False)
            elif topic == TOPIC_ALERT:
                data = json.loads(payload)
                alert_entry = {
                    "type":      data.get("type",    "unknown"),
                    "message":   data.get("message", ""),
                    "lat":       data.get("lat",     0.0),
                    "lon":       data.get("lon",     0.0),
                    "time":      datetime.now().strftime("%H:%M:%S"),
                }
                state["alerts"].insert(0, alert_entry)
                state["alerts"] = state["alerts"][:20]   # keep last 20

            state["last_updated"] = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

            # Append to trip history
            snapshot = {k: v for k, v in state.items() if k != "alerts"}
            snapshot["timestamp"] = state["last_updated"]
            trip_history.append(snapshot)
            if len(trip_history) > HISTORY_MAX:
                trip_history.pop(0)

        except (ValueError, KeyError, json.JSONDecodeError) as exc:
            log.debug("Message parse error on %s: %s", topic, exc)

    # Emit to all connected dashboard clients
    socketio.emit("sensor_update", dict(state))


def start_mqtt():
    mqtt_client.on_connect = _on_connect
    mqtt_client.on_message = _on_message
    mqtt_client.reconnect_delay_set(min_delay=1, max_delay=30)
    try:
        mqtt_client.connect(MQTT_BROKER, MQTT_PORT, keepalive=60)
        mqtt_client.loop_forever()
    except Exception as exc:
        log.error("MQTT loop error: %s", exc)


# ---------------------------------------------------------------------------
# MediaPipe Eye Detection Worker
# ---------------------------------------------------------------------------
mp_face_mesh = mp.solutions.face_mesh

# Landmark indices for left/right eye (MediaPipe 468-point model)
LEFT_EYE_IDX  = [33, 160, 158, 133, 153, 144]
RIGHT_EYE_IDX = [362, 385, 387, 263, 373, 380]


def _eye_aspect_ratio(landmarks, eye_idx, w, h):
    """Compute Eye Aspect Ratio from MediaPipe face landmarks."""
    pts = [(landmarks[i].x * w, landmarks[i].y * h) for i in eye_idx]
    # Vertical distances
    A = math.dist(pts[1], pts[5])
    B = math.dist(pts[2], pts[4])
    # Horizontal distance
    C = math.dist(pts[0], pts[3])
    return (A + B) / (2.0 * C + 1e-6)


def eye_detection_worker():
    """Pull frames from ESP32-CAM stream and publish EAR values via MQTT."""
    log.info("Eye detection worker starting. Stream: %s", CAM_STREAM)

    closed_frames  = 0
    blink_count    = 0
    minute_start   = time.time()
    eye_closed_start = None
    eye_was_closed = False
    fatigue        = False
    fatigue_published = False

    with mp_face_mesh.FaceMesh(
        max_num_faces=1,
        refine_landmarks=True,
        min_detection_confidence=0.5,
        min_tracking_confidence=0.5,
    ) as face_mesh:

        cap = None
        while True:
            # (Re)open the stream if needed
            if cap is None or not cap.isOpened():
                log.info("Opening camera stream %s ...", CAM_STREAM)
                cap = cv2.VideoCapture(CAM_STREAM)
                time.sleep(2)
                if not cap.isOpened():
                    log.warning("Could not open stream – retrying in 5 s")
                    time.sleep(5)
                    continue

            ret, frame = cap.read()
            if not ret or frame is None:
                log.warning("Frame grab failed – reconnecting")
                cap.release()
                cap = None
                time.sleep(2)
                continue

            h, w = frame.shape[:2]
            rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
            result = face_mesh.process(rgb)

            ear_left = ear_right = 1.0

            if result.multi_face_landmarks:
                lm = result.multi_face_landmarks[0].landmark
                ear_left  = _eye_aspect_ratio(lm, LEFT_EYE_IDX,  w, h)
                ear_right = _eye_aspect_ratio(lm, RIGHT_EYE_IDX, w, h)

            ear_avg    = (ear_left + ear_right) / 2.0
            eye_closed = ear_avg < EAR_THRESHOLD
            now        = time.time()

            if eye_closed:
                closed_frames += 1
                if not eye_was_closed:
                    eye_closed_start = now
                    eye_was_closed   = True
                closure_sec = now - (eye_closed_start or now)

                if (closed_frames >= FATIGUE_CONSEC or closure_sec >= EYE_CLOSURE_MAX_SEC):
                    if not fatigue:
                        fatigue = True
                        log.warning("FATIGUE detected (EAR=%.3f)", ear_avg)
            else:
                if eye_was_closed:
                    blink_count  += 1
                    eye_was_closed = False
                closed_frames = 0
                closure_sec   = 0.0
                fatigue       = False
                fatigue_published = False

            # Publish EAR payload
            ear_payload = json.dumps({
                "ear_left":   round(ear_left,  3),
                "ear_right":  round(ear_right, 3),
                "ear_avg":    round(ear_avg,   3),
                "closed_frames": closed_frames,
                "closure_sec": round(closure_sec if eye_was_closed else 0.0, 2),
                "fatigue":    fatigue,
            })
            mqtt_client.publish(TOPIC_EAR, ear_payload)

            if fatigue and not fatigue_published:
                fatigue_published = True
                mqtt_client.publish(TOPIC_FATIGUE, ear_payload, retain=True)
                mqtt_client.publish(TOPIC_ALERT,
                    json.dumps({"source": "server", "type": "fatigue"}), retain=True)

            # Publish blink rate once per minute
            if now - minute_start >= 60.0:
                blink_payload = str(blink_count)
                mqtt_client.publish(TOPIC_BLINK_RATE, blink_payload)
                log.info("Blink rate: %d blinks/min", blink_count)
                # blink_count == 0 is skipped intentionally: it typically means
                # the face was not detected during that minute (e.g. at startup
                # or if the rider looks away), so it should not generate a false
                # low-blink alert.  A sustained zero count will be caught by the
                # eye-closure / fatigue logic instead.
                if blink_count < BLINK_MIN_PER_MIN and blink_count > 0:
                    mqtt_client.publish(TOPIC_ALERT,
                        json.dumps({"source": "server", "type": "low_blink",
                                    "blink_rate": blink_count}), retain=True)
                blink_count  = 0
                minute_start = now


# ---------------------------------------------------------------------------
# Flask Routes
# ---------------------------------------------------------------------------
@app.route("/")
def index():
    return send_from_directory(".", "dashboard.html")


@app.route("/api/state")
def api_state():
    with state_lock:
        return json.dumps(state), 200, {"Content-Type": "application/json"}


@app.route("/api/history")
def api_history():
    return json.dumps(trip_history[-500:]), 200, {"Content-Type": "application/json"}


@app.route("/api/alert/reset", methods=["POST"])
def api_alert_reset():
    with state_lock:
        state["alerts"] = []
        state["crash"]  = False
        state["fatigue"] = False
    mqtt_client.publish(TOPIC_CMD_RESET, "reset")
    return json.dumps({"status": "ok"}), 200, {"Content-Type": "application/json"}


@app.route("/api/threshold", methods=["POST"])
def api_threshold():
    from flask import request
    data = request.get_json(silent=True) or {}
    mqtt_client.publish(TOPIC_CMD_THRESHOLD, json.dumps(data))
    return json.dumps({"status": "ok"}), 200, {"Content-Type": "application/json"}


@app.route("/api/contact", methods=["POST"])
def api_contact():
    from flask import request
    data = request.get_json(silent=True) or {}
    mqtt_client.publish(TOPIC_CMD_CONTACT, json.dumps(data))
    return json.dumps({"status": "ok"}), 200, {"Content-Type": "application/json"}


# ---------------------------------------------------------------------------
# SocketIO events
# ---------------------------------------------------------------------------
@socketio.on("connect")
def on_connect():
    log.info("Dashboard client connected: %s", socketio.server.environ.get("REMOTE_ADDR", "?"))
    with state_lock:
        socketio.emit("sensor_update", dict(state))


@socketio.on("cmd_reset")
def on_cmd_reset(data):
    with state_lock:
        state["alerts"] = []
        state["crash"]  = False
    mqtt_client.publish(TOPIC_CMD_RESET, "reset")


@socketio.on("cmd_threshold")
def on_cmd_threshold(data):
    mqtt_client.publish(TOPIC_CMD_THRESHOLD, json.dumps(data))


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
if __name__ == "__main__":
    # Start MQTT in background thread
    mqtt_thread = threading.Thread(target=start_mqtt, daemon=True)
    mqtt_thread.start()

    # Start eye detection worker in background thread
    eye_thread = threading.Thread(target=eye_detection_worker, daemon=True)
    eye_thread.start()

    log.info("Starting web server on http://0.0.0.0:%d", FLASK_PORT)
    socketio.run(app, host="0.0.0.0", port=FLASK_PORT, debug=False)
