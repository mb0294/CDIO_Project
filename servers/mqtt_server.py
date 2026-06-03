"""
HTTP backend for Smart Helmet dashboard.

This module preserves the original server structure/state while replacing MQTT
transport with HTTP endpoints consumed by ESP32 firmware.
"""

import json
import logging
import threading
from datetime import datetime

from flask import Flask, send_from_directory, request
from flask_socketio import SocketIO

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
FLASK_PORT = 5000

# Eye detection thresholds (kept for behavior compatibility / dashboard context)
EAR_THRESHOLD = 0.20
EYE_CLOSURE_MAX_SEC = 2.0
FATIGUE_CONSEC = 20
BLINK_MIN_PER_MIN = 10

# ---------------------------------------------------------------------------
# Logging
# ---------------------------------------------------------------------------
logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s")
log = logging.getLogger(__name__)

# ---------------------------------------------------------------------------
# Shared state (thread-safe via a lock)
# ---------------------------------------------------------------------------
state_lock = threading.Lock()
state = {
    "gps": {"lat": 0.0, "lon": 0.0, "alt": 0.0},
    "heart_rate": 0,
    "spo2": 0,
    "alcohol": 0.0,
    "lean_angle": 0.0,
    "altitude": 0.0,
    "pressure": 0,
    "crash": False,
    "blink_rate": 0,
    "ear_avg": 1.0,
    "closure_sec": 0.0,
    "fatigue": False,
    "alerts": [],
    "last_updated": "",
}

# Commands served to polling firmware
commands_state = {
    "reset": False,
    "fatigue": {"value": False},
    "threshold": {},
    "contact": {},
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


def _append_snapshot_unlocked() -> None:
    snapshot = {k: v for k, v in state.items() if k != "alerts"}
    snapshot["timestamp"] = state["last_updated"]
    trip_history.append(snapshot)
    if len(trip_history) > HISTORY_MAX:
        trip_history.pop(0)


def _touch_state_unlocked() -> None:
    state["last_updated"] = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    _append_snapshot_unlocked()


def _broadcast_state() -> None:
    socketio.emit("sensor_update", dict(state))


def _safe_json_body(default=None):
    data = request.get_json(silent=True)
    if data is None:
        return {} if default is None else default
    return data


def _status_ok():
    return json.dumps({"status": "ok"}), 200, {"Content-Type": "application/json"}


# ---------------------------------------------------------------------------
# HTTP ingestion endpoints (MQTT topic equivalents)
# ---------------------------------------------------------------------------
@app.route("/api/sensors/gps", methods=["POST"])
def sensors_gps():
    data = _safe_json_body({})
    with state_lock:
        state["gps"] = {
            "lat": float(data.get("lat", 0.0)),
            "lon": float(data.get("lon", 0.0)),
            "alt": float(data.get("alt", 0.0)),
        }
        _touch_state_unlocked()
    _broadcast_state()
    return _status_ok()


@app.route("/api/sensors/heart_rate", methods=["POST"])
def sensors_heart_rate():
    data = _safe_json_body({})
    value = data.get("value", data)
    with state_lock:
        state["heart_rate"] = int(value)
        _touch_state_unlocked()
    _broadcast_state()
    return _status_ok()


@app.route("/api/sensors/spo2", methods=["POST"])
def sensors_spo2():
    data = _safe_json_body({})
    value = data.get("value", data)
    with state_lock:
        state["spo2"] = int(value)
        _touch_state_unlocked()
    _broadcast_state()
    return _status_ok()


@app.route("/api/sensors/alcohol", methods=["POST"])
def sensors_alcohol():
    data = _safe_json_body({})
    value = data.get("value", data)
    with state_lock:
        state["alcohol"] = float(value)
        _touch_state_unlocked()
    _broadcast_state()
    return _status_ok()


@app.route("/api/sensors/lean_angle", methods=["POST"])
def sensors_lean_angle():
    data = _safe_json_body({})
    value = data.get("value", data)
    with state_lock:
        state["lean_angle"] = float(value)
        _touch_state_unlocked()
    _broadcast_state()
    return _status_ok()


@app.route("/api/sensors/pressure", methods=["POST"])
def sensors_pressure():
    data = _safe_json_body({})
    value = data.get("value", data)
    with state_lock:
        state["pressure"] = int(value)
        _touch_state_unlocked()
    _broadcast_state()
    return _status_ok()


@app.route("/api/sensors/altitude", methods=["POST"])
def sensors_altitude():
    data = _safe_json_body({})
    value = data.get("value", data)
    with state_lock:
        state["altitude"] = float(value)
        _touch_state_unlocked()
    _broadcast_state()
    return _status_ok()


@app.route("/api/sensors/crash", methods=["POST"])
def sensors_crash():
    data = _safe_json_body({})
    value = data.get("value", data)
    with state_lock:
        state["crash"] = bool(value)
        _touch_state_unlocked()
    _broadcast_state()
    return _status_ok()


@app.route("/api/eye/blink_rate", methods=["POST"])
def eye_blink_rate():
    data = _safe_json_body({})
    value = data.get("value", data)
    with state_lock:
        state["blink_rate"] = int(value)
        _touch_state_unlocked()
    _broadcast_state()
    return _status_ok()


@app.route("/api/eye/ear", methods=["POST"])
def eye_ear():
    data = _safe_json_body({})
    with state_lock:
        state["ear_avg"] = float(data.get("ear_avg", state["ear_avg"]))
        state["closure_sec"] = float(data.get("closure_sec", state["closure_sec"]))
        fatigue_val = bool(data.get("fatigue", state["fatigue"]))
        state["fatigue"] = fatigue_val
        commands_state["fatigue"] = {"value": fatigue_val}
        _touch_state_unlocked()
    _broadcast_state()
    return _status_ok()


@app.route("/api/eye/fatigue", methods=["POST"])
def eye_fatigue():
    data = _safe_json_body({})
    fatigue_val = bool(data.get("fatigue", data.get("value", False)))
    with state_lock:
        state["fatigue"] = fatigue_val
        commands_state["fatigue"] = {"value": fatigue_val}
        _touch_state_unlocked()
    _broadcast_state()
    return _status_ok()


@app.route("/api/alert", methods=["POST"])
def api_alert():
    data = _safe_json_body({})
    with state_lock:
        alert_entry = {
            "type": data.get("type", "unknown"),
            "message": data.get("message", ""),
            "lat": data.get("lat", 0.0),
            "lon": data.get("lon", 0.0),
            "time": datetime.now().strftime("%H:%M:%S"),
        }
        state["alerts"].insert(0, alert_entry)
        state["alerts"] = state["alerts"][:20]
        _touch_state_unlocked()
    _broadcast_state()
    return _status_ok()


@app.route("/api/sensors/batch", methods=["POST"])
def sensors_batch():
    data = _safe_json_body({})
    with state_lock:
        if "gps" in data:
            gps_data = data.get("gps") or {}
            state["gps"] = {
                "lat": float(gps_data.get("lat", state["gps"].get("lat", 0.0))),
                "lon": float(gps_data.get("lon", state["gps"].get("lon", 0.0))),
                "alt": float(gps_data.get("alt", state["gps"].get("alt", 0.0))),
            }
        if "heart_rate" in data:
            state["heart_rate"] = int(data.get("heart_rate", state["heart_rate"]))
        if "spo2" in data:
            state["spo2"] = int(data.get("spo2", state["spo2"]))
        if "alcohol" in data:
            state["alcohol"] = float(data.get("alcohol", state["alcohol"]))
        if "lean_angle" in data:
            state["lean_angle"] = float(data.get("lean_angle", state["lean_angle"]))
        if "pressure" in data:
            state["pressure"] = int(data.get("pressure", state["pressure"]))
        if "altitude" in data:
            state["altitude"] = float(data.get("altitude", state["altitude"]))
        if "crash" in data:
            state["crash"] = bool(data.get("crash", state["crash"]))
        if "blink_rate" in data:
            state["blink_rate"] = int(data.get("blink_rate", state["blink_rate"]))
        if "ear" in data:
            ear = data.get("ear") or {}
            state["ear_avg"] = float(ear.get("ear_avg", state["ear_avg"]))
            state["closure_sec"] = float(ear.get("closure_sec", state["closure_sec"]))
            fatigue_val = bool(ear.get("fatigue", state["fatigue"]))
            state["fatigue"] = fatigue_val
            commands_state["fatigue"] = {"value": fatigue_val}
        if "fatigue" in data:
            fatigue_val = bool(data.get("fatigue", state["fatigue"]))
            state["fatigue"] = fatigue_val
            commands_state["fatigue"] = {"value": fatigue_val}

        _touch_state_unlocked()

    _broadcast_state()
    return _status_ok()


# ---------------------------------------------------------------------------
# Commands endpoints (replacing MQTT cmd topics)
# ---------------------------------------------------------------------------
@app.route("/api/commands/pending", methods=["GET"])
def commands_pending():
    with state_lock:
        payload = {
            "reset": bool(commands_state.get("reset", False)),
            "fatigue": dict(commands_state.get("fatigue", {})),
            "threshold": dict(commands_state.get("threshold", {})),
            "contact": dict(commands_state.get("contact", {})),
        }
        # one-shot reset command
        if commands_state.get("reset"):
            commands_state["reset"] = False

    return json.dumps(payload), 200, {"Content-Type": "application/json"}


@app.route("/api/commands/reset", methods=["POST"])
def commands_reset():
    with state_lock:
        commands_state["reset"] = True
        state["alerts"] = []
        state["crash"] = False
        state["fatigue"] = False
        commands_state["fatigue"] = {"value": False}
        _touch_state_unlocked()
    _broadcast_state()
    return _status_ok()


@app.route("/api/commands/threshold", methods=["POST"])
def commands_threshold():
    data = _safe_json_body({})
    with state_lock:
        commands_state["threshold"] = data
    return _status_ok()


@app.route("/api/commands/contact", methods=["POST"])
def commands_contact():
    data = _safe_json_body({})
    with state_lock:
        commands_state["contact"] = data
    return _status_ok()


# Backward-compatible dashboard endpoints
@app.route("/api/alert/reset", methods=["POST"])
def api_alert_reset():
    return commands_reset()


@app.route("/api/threshold", methods=["POST"])
def api_threshold():
    return commands_threshold()


@app.route("/api/contact", methods=["POST"])
def api_contact():
    return commands_contact()


# ---------------------------------------------------------------------------
# Dashboard data endpoints
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


# ---------------------------------------------------------------------------
# SocketIO events
# ---------------------------------------------------------------------------
@socketio.on("connect")
def on_connect():
    with state_lock:
        socketio.emit("sensor_update", dict(state))


@socketio.on("cmd_reset")
def on_cmd_reset(_data):
    commands_reset()


@socketio.on("cmd_threshold")
def on_cmd_threshold(data):
    with state_lock:
        commands_state["threshold"] = data or {}


@socketio.on("cmd_contact")
def on_cmd_contact(data):
    with state_lock:
        commands_state["contact"] = data or {}


# ---------------------------------------------------------------------------
# Startup
# ---------------------------------------------------------------------------
def run_server(host: str = "0.0.0.0", port: int = FLASK_PORT):
    log.info("Starting HTTP server on http://%s:%d", host, port)
    socketio.run(app, host=host, port=port, debug=False)


if __name__ == "__main__":
    run_server()
