# 🪖 Smart Helmet - HTTP Architecture

## 📋 Vue d'Ensemble

Le projet est maintenant **100% HTTP** (migration complète MQTT → HTTP) en conservant la même logique fonctionnelle :

- ESP32 (capteurs) envoie les données via `POST /api/sensors/batch`
- ESP32 récupère les commandes via `GET /api/commands/pending`
- Serveur Python diffuse l'état en temps réel vers le dashboard via Socket.IO

## 🏗️ Architecture

```text
ESP32 (capteurs + alertes) -> HTTP Server (Flask) -> Web Dashboard (Socket.IO)
```

## 📁 Structure du Projet

```text
CDIO_Project/
├── firmwares/
│   ├── mqtt_system/
│   │   ├── main_esp32_controller.ino   # Firmware principal migré HTTP
│   │   ├── http_config.h               # Configuration HTTP
│   │   └── sensor_config.h
│   └── http_system/
│       └── eye.ino
├── servers/
│   ├── mqtt_server.py                  # Backend HTTP (nom conservé pour compatibilité)
│   ├── http_server.py                  # Entrypoint HTTP
│   ├── http_detector.py
│   └── dashboard.html
├── requirements/
│   └── requirements_clean.txt
├── MIGRATION_GUIDE.md
└── GUIDE_INSTALLATION.md
```

## 🚀 Installation

```bash
pip install -r requirements/requirements_clean.txt
```

## 🎯 Exécution

### 1) Firmware ESP32

1. Ouvrir `firmwares/mqtt_system/main_esp32_controller.ino`
2. Configurer WiFi + URL serveur dans `firmwares/mqtt_system/http_config.h`
3. Configurer les capteurs dans `firmwares/mqtt_system/sensor_config.h`
4. Flasher l'ESP32

### 2) Serveur HTTP

```bash
python servers/http_server.py
```

Dashboard: `http://localhost:5000`

## 🔧 Endpoints clés

- `POST /api/sensors/batch`
- `POST /api/alert`
- `GET  /api/commands/pending`
- `POST /api/commands/reset`
- `POST /api/commands/threshold`
- `POST /api/commands/contact`

## 📚 Migration détaillée

Voir `MIGRATION_GUIDE.md` pour le mapping MQTT topics → endpoints HTTP, la stratégie polling, retry/backoff et les impacts performance.
