"""
drowsiness_detector.py
TheInfoFlux — ESP32-CAM Drowsiness / Fatigue Detector

Features:
  - MJPEG stream pull from ESP32-CAM
  - MediaPipe FaceMesh — 478 landmarks
  - EAR (Eye Aspect Ratio) drowsiness detection
  - MAR (Mouth Aspect Ratio) yawn detection
  - Drowsiness score / timer overlay
  - Snapshot saved on alert
  - GPIO buzzer via ESP32-CAM HTTP endpoint
  - Web dashboard (http://localhost:5000)

Requirements:
  pip install opencv-python mediapipe scipy flask flask-socketio requests

Usage:
  python drowsiness_detector.py --esp32 http://192.168.x.x
"""

import argparse
import base64
import datetime
import os
import threading
import time
import urllib.request

import cv2
import mediapipe as mp
import numpy as np
import requests
from flask import Flask, Response, render_template_string
from flask_socketio import SocketIO
from scipy.spatial import distance as dist

# ─── CLI args ───────────────────────────────────────────────────────────────
parser = argparse.ArgumentParser()
parser.add_argument("--esp32", default="http://192.168.43.144",
                    help="ESP32-CAM base URL, e.g. http://192.168.43.144")
parser.add_argument("--ear-thresh", type=float, default=0.15,
                    help="EAR threshold below which eye is 'closed' (default 0.25)")
parser.add_argument("--mar-thresh", type=float, default=0.6,
                    help="MAR threshold above which mouth is 'open/yawning' (default 0.6)")
parser.add_argument("--consec-frames", type=int, default=20,
                    help="Consecutive closed-eye frames before DROWSY alert (default 20)")
parser.add_argument("--yawn-frames", type=int, default=15,
                    help="Consecutive open-mouth frames before YAWN alert (default 15)")
parser.add_argument("--snapshots-dir", default="snapshots",
                    help="Directory to save alert snapshots (default ./snapshots)")
args = parser.parse_args()

ESP32_URL    = args.esp32.rstrip("/")
STREAM_URL   = f"{ESP32_URL}/stream"
BUZZER_ON    = f"http://192.168.43.144:81/buzzer?state=on"
BUZZER_OFF   = f"http://192.168.43.144:81/buzzer?state=off"

EAR_THRESH   = args.ear_thresh
MAR_THRESH   = args.mar_thresh
EYE_CONSEC   = args.consec_frames
YAWN_CONSEC  = args.yawn_frames
SNAP_DIR     = args.snapshots_dir
os.makedirs(SNAP_DIR, exist_ok=True)

# ─── MediaPipe landmark indices (FaceMesh 478-point model) ──────────────────
# Left eye
L_EYE  = [362, 385, 387, 263, 373, 380]
# Right eye
R_EYE  = [33,  160, 158, 133, 153, 144]
# Mouth (vertical + horizontal landmarks for MAR)
MOUTH_VERT  = [13, 14, 312, 82]    # top-lip, bottom-lip inner pts
MOUTH_HORIZ = [78, 308]            # left, right corners

# ─── Flask / SocketIO app ───────────────────────────────────────────────────
app = Flask(__name__)
app.config["SECRET_KEY"] = "drowsy-secret"
sio = SocketIO(app, cors_allowed_origins="*", async_mode="threading")

DASHBOARD_HTML = """
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<title>Drowsiness Detector — TheInfoFlux</title>
<style>
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body { background: #0f0f12; color: #e0e0e0; font-family: 'Segoe UI', sans-serif; min-height: 100vh; }
  header { background: #1a1a22; border-bottom: 2px solid #e84040; padding: 14px 24px;
           display: flex; align-items: center; gap: 12px; }
  header h1 { font-size: 1.3rem; font-weight: 600; letter-spacing: 0.5px; }
  header span { font-size: 0.78rem; color: #888; margin-left: auto; }
  .layout { display: grid; grid-template-columns: 1fr 340px; gap: 16px; padding: 16px; }
  .video-card { background: #1a1a22; border-radius: 10px; overflow: hidden; }
  .video-card img { width: 100%; display: block; }
  .sidebar { display: flex; flex-direction: column; gap: 14px; }
  .card { background: #1a1a22; border-radius: 10px; padding: 16px; }
  .card h2 { font-size: 0.85rem; color: #888; text-transform: uppercase;
              letter-spacing: 1px; margin-bottom: 12px; }
  .alert-banner { border-radius: 8px; padding: 12px 16px; font-size: 1rem;
                  font-weight: 600; text-align: center; transition: all 0.3s; }
  .alert-ok      { background: #1b3a2a; color: #4ade80; border: 1px solid #2d6b45; }
  .alert-drowsy  { background: #3a1b1b; color: #f87171; border: 1px solid #6b2d2d;
                   animation: pulse 0.8s ease-in-out infinite; }
  .alert-yawn    { background: #3a2e1b; color: #fbbf24; border: 1px solid #6b5a2d; }
  @keyframes pulse { 0%,100%{opacity:1} 50%{opacity:0.7} }
  .metric { display: flex; justify-content: space-between; align-items: center;
            padding: 8px 0; border-bottom: 1px solid #2a2a35; }
  .metric:last-child { border-bottom: none; }
  .metric label { font-size: 0.85rem; color: #aaa; }
  .metric value { font-size: 1.0rem; font-weight: 600; font-variant-numeric: tabular-nums; }
  .bar-wrap { background: #2a2a35; border-radius: 4px; height: 6px; margin-top: 4px; overflow: hidden; }
  .bar-fill  { height: 100%; border-radius: 4px; transition: width 0.2s, background 0.3s; }
  .snap-list { max-height: 180px; overflow-y: auto; }
  .snap-item { font-size: 0.78rem; color: #aaa; padding: 4px 0;
               border-bottom: 1px solid #2a2a35; cursor: pointer; }
  .snap-item:hover { color: #fff; }
  .conn-dot { width: 8px; height: 8px; border-radius: 50%; display: inline-block;
              background: #4ade80; margin-right: 6px; }
  .conn-dot.off { background: #f87171; }
</style>
</head>
<body>
<header>
  <span id="conn-dot" class="conn-dot off"></span>
  <h1>Drowsiness Detector</h1>
  <span id="time-label">--:--:--</span>
</header>
<div class="layout">
  <div class="video-card">
    <img id="feed" src="/video_feed" alt="Live feed">
  </div>
  <div class="sidebar">
    <div class="card">
      <h2>Status</h2>
      <div id="alert-banner" class="alert-banner alert-ok">ALERT: OK</div>
    </div>
    <div class="card">
      <h2>Metrics</h2>
      <div class="metric">
        <label>EAR (eyes)</label>
        <value id="ear-val">--</value>
      </div>
      <div class="bar-wrap"><div id="ear-bar" class="bar-fill" style="width:0%;background:#4ade80"></div></div>
      <div class="metric" style="margin-top:10px">
        <label>MAR (mouth)</label>
        <value id="mar-val">--</value>
      </div>
      <div class="bar-wrap"><div id="mar-bar" class="bar-fill" style="width:0%;background:#fbbf24"></div></div>
      <div class="metric" style="margin-top:10px">
        <label>Drowsy frames</label>
        <value id="eye-frames">0</value>
      </div>
      <div class="metric">
        <label>Yawn frames</label>
        <value id="yawn-frames">0</value>
      </div>
      <div class="metric">
        <label>Drowsy events</label>
        <value id="drowsy-count">0</value>
      </div>
      <div class="metric">
        <label>Yawn events</label>
        <value id="yawn-count">0</value>
      </div>
      <div class="metric">
        <label>Session time</label>
        <value id="session-time">0s</value>
      </div>
    </div>
    <div class="card">
      <h2>Snapshots</h2>
      <div id="snap-list" class="snap-list"><p style="color:#555;font-size:0.8rem">No snapshots yet.</p></div>
    </div>
  </div>
</div>
<script src="https://cdnjs.cloudflare.com/ajax/libs/socket.io/4.6.1/socket.io.min.js"></script>
<script>
  const socket = io();
  const banner = document.getElementById('alert-banner');
  const connDot = document.getElementById('conn-dot');
  let sessionStart = Date.now();

  socket.on('connect', () => { connDot.classList.remove('off'); });
  socket.on('disconnect', () => { connDot.classList.add('off'); });

  socket.on('stats', (d) => {
    document.getElementById('ear-val').textContent = d.ear.toFixed(3);
    document.getElementById('mar-val').textContent = d.mar.toFixed(3);
    document.getElementById('eye-frames').textContent = d.eye_frames;
    document.getElementById('yawn-frames').textContent = d.yawn_frames;
    document.getElementById('drowsy-count').textContent = d.drowsy_events;
    document.getElementById('yawn-count').textContent = d.yawn_events;

    // EAR bar (0.5 = fully open, 0 = fully closed)
    const earPct = Math.min(100, (d.ear / 0.5) * 100);
    const earBar = document.getElementById('ear-bar');
    earBar.style.width = earPct + '%';
    earBar.style.background = d.ear < {{ ear_thresh }} ? '#f87171' : '#4ade80';

    // MAR bar
    const marPct = Math.min(100, (d.mar / 1.0) * 100);
    const marBar = document.getElementById('mar-bar');
    marBar.style.width = marPct + '%';
    marBar.style.background = d.mar > {{ mar_thresh }} ? '#f87171' : '#fbbf24';

    // Alert banner
    if (d.alert === 'DROWSY') {
      banner.textContent = '⚠ DROWSY ALERT';
      banner.className = 'alert-banner alert-drowsy';
    } else if (d.alert === 'YAWN') {
      banner.textContent = '😮 YAWN DETECTED';
      banner.className = 'alert-banner alert-yawn';
    } else {
      banner.textContent = '✓ ALERT: OK';
      banner.className = 'alert-banner alert-ok';
    }
  });

  socket.on('snapshot', (d) => {
    const list = document.getElementById('snap-list');
    if (list.querySelector('p')) list.innerHTML = '';
    const item = document.createElement('div');
    item.className = 'snap-item';
    item.textContent = d.filename + ' (' + d.reason + ')';
    item.title = d.path;
    list.prepend(item);
  });

  // Clock
  setInterval(() => {
    const now = new Date();
    document.getElementById('time-label').textContent = now.toLocaleTimeString();
    const elapsed = Math.floor((Date.now() - sessionStart) / 1000);
    document.getElementById('session-time').textContent = elapsed + 's';
  }, 1000);
</script>
</body>
</html>
""".replace("{{ ear_thresh }}", str(EAR_THRESH)).replace("{{ mar_thresh }}", str(MAR_THRESH))

# ─── Shared state ────────────────────────────────────────────────────────────
state = {
    "ear": 0.0,
    "mar": 0.0,
    "eye_frames": 0,
    "yawn_frames": 0,
    "drowsy_events": 0,
    "yawn_events": 0,
    "alert": "OK",   # "OK" | "DROWSY" | "YAWN"
    "frame": None,   # latest annotated JPEG bytes
    "lock": threading.Lock(),
}

# ─── Geometry helpers ────────────────────────────────────────────────────────
def _pts(landmarks, indices, iw, ih):
    return np.array(
        [(landmarks[i].x * iw, landmarks[i].y * ih) for i in indices],
        dtype=np.float32,
    )

def eye_aspect_ratio(eye_pts):
    """EAR = (‖p2-p6‖ + ‖p3-p5‖) / (2 × ‖p1-p4‖)"""
    A = dist.euclidean(eye_pts[1], eye_pts[5])
    B = dist.euclidean(eye_pts[2], eye_pts[4])
    C = dist.euclidean(eye_pts[0], eye_pts[3])
    return (A + B) / (2.0 * C + 1e-6)

def mouth_aspect_ratio(mouth_vert_pts, mouth_horiz_pts):
    """MAR = vertical distance / horizontal distance"""
    vert = dist.euclidean(mouth_vert_pts[0], mouth_vert_pts[1])
    horiz = dist.euclidean(mouth_horiz_pts[0], mouth_horiz_pts[1])
    return vert / (horiz + 1e-6)

# ─── Buzzer helpers ──────────────────────────────────────────────────────────
def buzzer(on: bool):
    url = BUZZER_ON if on else BUZZER_OFF
    try:
        r = requests.get(url, timeout=0.5)
        print(f"[BUZZER] {'ON' if on else 'OFF'} → {r.status_code}")
    except Exception as e:
        print(f"[BUZZER] FAILED → {e}")

def beep_pattern(times=3, on_ms=200, off_ms=150):
    """Short async beep pattern for alerts"""
    def _beep():
        for _ in range(times):
            buzzer(True)
            time.sleep(1.0)
            buzzer(False)
            time.sleep(off_ms / 1000)
    threading.Thread(target=_beep, daemon=True).start()

# ─── Snapshot ────────────────────────────────────────────────────────────────
def save_snapshot(frame, reason):
    ts = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    filename = f"{reason}_{ts}.jpg"
    path = os.path.join(SNAP_DIR, filename)
    cv2.imwrite(path, frame)
    sio.emit("snapshot", {"filename": filename, "path": path, "reason": reason})
    print(f"[SNAP] Saved {path}")

# ─── Detection thread ────────────────────────────────────────────────────────
def detection_loop():
    mp_face = mp.solutions.face_mesh
    face_mesh = mp_face.FaceMesh(
        max_num_faces=1,
        refine_landmarks=True,
        min_detection_confidence=0.5,
        min_tracking_confidence=0.5,
    )

    eye_counter  = 0
    yawn_counter = 0
    drowsy_events = 0
    yawn_events   = 0
    in_drowsy_alert = False
    in_yawn_alert   = False
    last_alert_time = 0

    stream = urllib.request.urlopen(STREAM_URL, timeout=10)
    raw_bytes = b""

    print(f"[STREAM] Connected to {STREAM_URL}")

    while True:
        chunk = stream.read(4096)
        if not chunk:
            break
        raw_bytes += chunk

        # Extract JPEG frame from MJPEG boundary
        a = raw_bytes.find(b"\xff\xd8")   # JPEG start
        b = raw_bytes.find(b"\xff\xd9")   # JPEG end
        if a == -1 or b == -1:
            continue

        jpg = raw_bytes[a : b + 2]
        raw_bytes = raw_bytes[b + 2:]

        frame = cv2.imdecode(np.frombuffer(jpg, np.uint8), cv2.IMREAD_COLOR)
        if frame is None:
            continue
        frame = cv2.flip(frame, 0)

        ih, iw = frame.shape[:2]
        rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        results = face_mesh.process(rgb)

        ear = 0.0
        mar = 0.0
        alert = "OK"
        annotated = frame.copy()

        if results.multi_face_landmarks:
            lm = results.multi_face_landmarks[0].landmark

            # ── EAR ──────────────────────────────────────────────────────
            left_eye  = _pts(lm, L_EYE,  iw, ih)
            right_eye = _pts(lm, R_EYE,  iw, ih)
            left_ear  = eye_aspect_ratio(left_eye)
            right_ear = eye_aspect_ratio(right_eye)
            ear = (left_ear + right_ear) / 2.0

            # ── MAR ──────────────────────────────────────────────────────
            m_vert  = _pts(lm, MOUTH_VERT,  iw, ih)
            m_horiz = _pts(lm, MOUTH_HORIZ, iw, ih)
            mar = mouth_aspect_ratio(m_vert, m_horiz)

            # ── Counters ─────────────────────────────────────────────────
            if ear < EAR_THRESH:
                eye_counter += 1
            else:
                eye_counter = 0
                if in_drowsy_alert:
                    in_drowsy_alert = False
                    buzzer(False)

            if mar > MAR_THRESH:
                yawn_counter += 1
            else:
                yawn_counter = 0
                if in_yawn_alert:
                    in_yawn_alert = False

            # ── Alerts ───────────────────────────────────────────────────
            if eye_counter >= EYE_CONSEC:
                alert = "DROWSY"
                if not in_drowsy_alert:
                    in_drowsy_alert = True
                    drowsy_events += 1
                    beep_pattern(times=4, on_ms=300, off_ms=100)
                    save_snapshot(annotated, "DROWSY")
                    print(f"[ALERT] DROWSY — EAR={ear:.3f}")

            elif yawn_counter >= YAWN_CONSEC:
                alert = "YAWN"
                if not in_yawn_alert:
                    in_yawn_alert = True
                    yawn_events += 1
                    beep_pattern(times=2, on_ms=150, off_ms=100)
                    save_snapshot(annotated, "YAWN")
                    print(f"[ALERT] YAWN — MAR={mar:.3f}")

            # ── Draw eye contours ─────────────────────────────────────────
            eye_color = (0, 0, 255) if alert == "DROWSY" else (0, 255, 0)
            cv2.polylines(annotated, [left_eye.astype(int)],  True, eye_color, 1)
            cv2.polylines(annotated, [right_eye.astype(int)], True, eye_color, 1)
            mouth_pts = _pts(lm, MOUTH_HORIZ + MOUTH_VERT, iw, ih)
            mouth_color = (0, 165, 255) if alert == "YAWN" else (0, 200, 200)
            cv2.polylines(annotated, [mouth_pts.astype(int)[:2]], False, mouth_color, 1)

        # ── HUD overlay ───────────────────────────────────────────────────
        overlay = annotated.copy()
        cv2.rectangle(overlay, (0, 0), (iw, 70), (0, 0, 0), -1)
        cv2.addWeighted(overlay, 0.55, annotated, 0.45, 0, annotated)

        # Status bar
        if alert == "DROWSY":
            bar_color = (0, 0, 200)
            label_txt = "DROWSY ALERT"
        elif alert == "YAWN":
            bar_color = (0, 130, 220)
            label_txt = "YAWN DETECTED"
        else:
            bar_color = (0, 140, 70)
            label_txt = "ALERT: OK"

        cv2.rectangle(annotated, (0, 0), (iw, 28), bar_color, -1)
        cv2.putText(annotated, label_txt, (8, 20),
                    cv2.FONT_HERSHEY_DUPLEX, 0.6, (255, 255, 255), 1, cv2.LINE_AA)

        # Score text
        cv2.putText(annotated, f"EAR:{ear:.3f}  Frames:{eye_counter}",
                    (8, 50), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (200, 255, 200), 1, cv2.LINE_AA)
        cv2.putText(annotated, f"MAR:{mar:.3f}  Yawn Frames:{yawn_counter}",
                    (8, 66), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (200, 230, 255), 1, cv2.LINE_AA)

        # Timer if drowsy
        if alert == "DROWSY":
            secs = eye_counter / 30.0  # approx seconds
            cv2.putText(annotated, f"EYES CLOSED {secs:.1f}s",
                        (iw - 200, 20), cv2.FONT_HERSHEY_SIMPLEX, 0.55,
                        (0, 100, 255), 2, cv2.LINE_AA)

        # Encode to JPEG
        _, jpeg = cv2.imencode(".jpg", annotated, [cv2.IMWRITE_JPEG_QUALITY, 80])

        with state["lock"]:
            state["ear"] = ear
            state["mar"] = mar
            state["eye_frames"] = eye_counter
            state["yawn_frames"] = yawn_counter
            state["drowsy_events"] = drowsy_events
            state["yawn_events"] = yawn_events
            state["alert"] = alert
            state["frame"] = jpeg.tobytes()

        # Push stats to dashboard via SocketIO
        sio.emit("stats", {
            "ear": ear,
            "mar": mar,
            "eye_frames": eye_counter,
            "yawn_frames": yawn_counter,
            "drowsy_events": drowsy_events,
            "yawn_events": yawn_events,
            "alert": alert,
        })

    face_mesh.close()
    print("[STREAM] Disconnected.")

# ─── Flask routes ────────────────────────────────────────────────────────────
@app.route("/")
def index():
    return render_template_string(DASHBOARD_HTML)

@app.route("/video_feed")
def video_feed():
    def gen():
        while True:
            with state["lock"]:
                frame = state["frame"]
            if frame:
                yield (b"--frame\r\nContent-Type: image/jpeg\r\n\r\n" + frame + b"\r\n")
            time.sleep(0.033)  # ~30 fps ceiling
    return Response(gen(), mimetype="multipart/x-mixed-replace; boundary=frame")

# ─── Entry point ─────────────────────────────────────────────────────────────
if __name__ == "__main__":
    print(f"[CONFIG] ESP32-CAM  : {ESP32_URL}")
    print(f"[CONFIG] EAR thresh : {EAR_THRESH}")
    print(f"[CONFIG] MAR thresh : {MAR_THRESH}")
    print(f"[CONFIG] Eye frames : {EYE_CONSEC}")
    print(f"[CONFIG] Yawn frames: {YAWN_CONSEC}")
    print(f"[CONFIG] Snapshots  : {SNAP_DIR}/")
    print()
    print("[WEB] Dashboard → http://localhost:5000")
    print()

    # Start detection in background thread
    t = threading.Thread(target=detection_loop, daemon=True)
    t.start()

    # Start Flask + SocketIO
    sio.run(app, host="0.0.0.0", port=5000, debug=False, allow_unsafe_werkzeug=True)
