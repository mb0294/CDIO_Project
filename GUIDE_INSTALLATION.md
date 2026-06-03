# 🪖 Smart Helmet - Guide d'Installation (HTTP)

## 📋 Prérequis

- Arduino IDE + package ESP32
- Python 3.11+
- ESP32 Dev Module / ESP32-CAM selon votre montage

## 1) Installation Python

```bash
pip install -r requirements/requirements_clean.txt
```

## 2) Configuration Firmware

Fichier principal:
- `firmwares/mqtt_system/main_esp32_controller.ino`

Configurer:
- `firmwares/mqtt_system/http_config.h`
  - `WIFI_SSID`
  - `WIFI_PASSWORD`
  - `HTTP_SERVER_URL` (ex: `http://192.168.1.100:5000`)
- `firmwares/mqtt_system/sensor_config.h`

## 3) Upload ESP32

Dans Arduino IDE:
- Board: **ESP32 Dev Module**
- CPU Frequency: 240MHz
- Flash Size: 4MB

Puis flasher `main_esp32_controller.ino`.

## 4) Lancement Serveur HTTP

```bash
python servers/http_server.py
```

Le dashboard est disponible sur:
- `http://localhost:5000`

## 5) Vérification rapide

- L'ESP32 envoie un batch sur `POST /api/sensors/batch`
- Le serveur met à jour l'état et pousse `sensor_update` au dashboard
- L'ESP32 récupère les commandes via `GET /api/commands/pending`

## 6) Commandes Dashboard disponibles

- Reset: `POST /api/commands/reset`
- Seuils: `POST /api/commands/threshold`
- Contacts: `POST /api/commands/contact`

## Notes migration

La communication MQTT a été remplacée par HTTP avec:
- batching des capteurs
- polling des commandes
- retry exponentiel côté firmware

Voir `MIGRATION_GUIDE.md` pour le détail complet de la migration.
