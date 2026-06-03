# 🪖 Smart Helmet - HTTP Drowsiness Detection

## 📌 Statut du projet
Ce dépôt utilise désormais **l'architecture HTTP** comme solution standard.

- ✅ Recommandé: `servers/http_detector.py`
- ⚠️ MQTT: conservé uniquement comme système **obsolète / archivé**

La détection oculaire MediaPipe est fiable avec l'architecture HTTP (stream direct ESP32-CAM), contrairement à l'ancien flux MQTT.

## 🏗️ Architecture standard
```text
ESP32-CAM -> HTTP stream -> http_detector.py -> Dashboard (http://localhost:5000)
```

## 📁 Structure utile
```text
CDIO_Project/
├── firmwares/http_system/eye.ino
├── servers/http_detector.py
├── servers/dashboard.html
├── requirements/requirements_clean.txt
├── MIGRATION_GUIDE.md
└── ARCHIVED_SYSTEMS.md
```

## 🚀 Installation rapide
### 1) Dépendances Python (HTTP)
```bash
pip install -r requirements/requirements_clean.txt
```

### 2) Flasher l'ESP32-CAM
- Ouvrir `firmwares/http_system/eye.ino`
- Configurer le Wi-Fi (SSID/mot de passe)
- Board: **AI Thinker ESP32-CAM**
- Flasher puis relever l'adresse IP affichée au Serial Monitor

### 3) Lancer la détection
```bash
python servers/http_detector.py --esp32 http://192.168.1.100
```

### 4) Ouvrir le dashboard
- `http://localhost:5000`

## 🧭 Migration depuis MQTT
Si vous venez du système MQTT:
1. Installez `requirements/requirements_clean.txt`
2. Utilisez `servers/http_detector.py` comme serveur principal
3. Consultez `MIGRATION_GUIDE.md` pour les détails de migration et dépannage

## 🛠️ Dépannage rapide
- ESP32 non accessible: vérifier IP, Wi-Fi, alimentation 5V
- Dashboard vide: vérifier URL `--esp32` et accès à `http://IP_ESP32/stream`
- Dépendances: réinstaller `requirements_clean.txt`

---
Consultez `GUIDE_INSTALLATION.md` pour le guide d'installation HTTP détaillé.
