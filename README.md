# 🪖 Smart Helmet - ESP32-CAM Drowsiness Detection

## 📋 Vue d'Ensemble

Smart Helmet est un système de détection de somnolence en temps réel basé sur ESP32-CAM avec deux architectures disponibles:
- **Système MQTT** - Architecture complète avec capteurs multiples et communication MQTT
- **Système HTTP** - Architecture simplifiée avec streaming HTTP direct

## 🏗️ Architecture

### Option 1: Système MQTT (Complet)
```
ESP32-CAM (Vidéo) → MQTT → Python Server → Web Dashboard
ESP32-S (Capteurs) → MQTT → Python Server → Web Dashboard
```

### Option 2: Système HTTP (Simplifié)
```
ESP32-CAM → HTTP Stream → Python Detector → Web Dashboard
```

## 📁 Structure du Projet

```
CDIO_Project/
├── firmwares/
│   ├── mqtt_system/              # Système MQTT complet
│   │   ├── esp32_cam_eye_detection.ino
│   │   ├── main_esp32_controller.ino
│   │   ├── mqtt_config.h
│   │   └── sensor_config.h
│   └── http_system/              # Système HTTP simplifié
│       └── eye.ino
├── servers/
│   ├── mqtt_server.py            # Serveur MQTT
│   ├── http_detector.py          # Détecteur HTTP
│   └── dashboard.html            # Dashboard web
├── requirements/
│   ├── requirements_mqtt.txt     # Dépendances MQTT
│   └── requirements_clean.txt    # Dépendances HTTP
├── tests/
│   └── sensor_tests/             # Tests capteurs
└── README.md
```

## 🚀 Installation

### Prérequis
- Arduino IDE avec ESP32 board package
- Python 3.11+
- ESP32-CAM (AI Thinker)

### Installation Python

**Pour système MQTT:**
```bash
pip install -r requirements/requirements_mqtt.txt
```

**Pour système HTTP:**
```bash
pip install -r requirements/requirements_clean.txt
```

## 🎯 Utilisation

### ⚡ Option Recommandée: Système HTTP (Détection Oculaire Complète)

**Pourquoi:** Système HTTP avec détection oculaire MediaPipe complète et fonctionnelle.

**Flashage ESP32-CAM:**
1. Ouvrir `firmwares/http_system/eye.ino`
2. Modifier WiFi (lignes 22-23)
3. Board: ESP32-CAM, 240MHz, 4MB
4. Flasher

**Lancement détecteur:**
```bash
python servers/http_detector.py --esp32 http://192.168.1.100
```

**Dashboard:** `http://localhost:5000`

### Option 2: Système MQTT (Capteurs Seulement)

**Note:** La détection oculaire est désactivée dans ce système (incompatibilité MediaPipe). Utilisez le système HTTP pour la détection oculaire.

**Flashage ESP32-CAM:**
1. Ouvrir `firmwares/mqtt_system/esp32_cam_eye_detection.ino`
2. Configurer WiFi dans `mqtt_config.h`
3. Board: ESP32-CAM, 240MHz, 4MB
4. Flasher

**Flashage ESP32-S (optionnel):**
1. Ouvrir `firmwares/mqtt_system/main_esp32_controller.ino`
2. Configurer capteurs dans `sensor_config.h`
3. Board: ESP32 Dev Module
4. Flasher

**Lancement serveur:**
```bash
python servers/mqtt_server.py
```

**Dashboard:** `http://localhost:5000`

## 🔧 Fonctionnalités

### Système MQTT (Capteurs Seulement)
- **Capteurs multiples** (GPS, cœur, SpO2, alcool, pression)
- **Communication bidirectionnelle** via MQTT
- **Alertes temps réel**
- **Note:** Détection oculaire désactivée (incompatibilité MediaPipe)

### Système HTTP (Détection Oculaire Complète) ⚡
- **Streaming MJPEG** direct
- **Détection yeux fermés** (EAR - Eye Aspect Ratio)
- **Détection bâillements** (MAR - Mouth Aspect Ratio)
- **Alertes LED** intégrées
- **Dashboard temps réel** avec statistiques
- **Snapshots** automatiques sur alerte

## 📊 Métriques

- **EAR** < 0.15 → Yeux fermés
- **MAR** > 0.6 → Bâillement
- **20 frames** → Alert DROWSY
- **15 frames** → Alert YAWN

## 🛠️ Matériel Requis

### Système MQTT
- ESP32-CAM (AI Thinker)
- ESP32-S Dev Module
- Capteurs: GPS, MAX30102, MPU-9250, MQ-3, BMP280, SIM800L

### Système HTTP
- ESP32-CAM (AI Thinker)
- LED (GPIO 14)
- Alimentation 5V 2A

## 🐛 Dépannage

### Problèmes Communs
1. **ESP32 non détecté**: Vérifier pilotes USB
2. **WiFi échoue**: Vérifier SSID/mot de passe
3. **Dashboard vide**: Vérifier connexion ESP32-PC

### Outils Debug
- Serial Monitor: 115200 baud
- Browser DevTools
- MQTT Explorer (système MQTT)

---

**Smart Helmet** - Votre compagnon de sécurité sur la route ! 🏍️💨
# cdio_ava1
