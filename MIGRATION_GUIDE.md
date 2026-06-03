# MQTT → HTTP Migration Guide

## Objectif

Remplacer MQTT par HTTP tout en gardant le même comportement fonctionnel:
- mêmes données JSON
- mêmes événements métier (alertes, reset, fatigue)
- même diffusion dashboard via Socket.IO

## Mapping Topics MQTT → Endpoints HTTP

| MQTT Topic | HTTP Endpoint | Méthode |
|---|---|---|
| `helmet/sensors/*` | `/api/sensors/batch` | `POST` |
| `helmet/alert` | `/api/alert` | `POST` |
| `helmet/cmd/reset` | `/api/commands/reset` (émission) + `/api/commands/pending` (lecture) | `POST` + `GET` |
| `helmet/cmd/threshold` | `/api/commands/threshold` + `/api/commands/pending` | `POST` + `GET` |
| `helmet/cmd/contact` | `/api/commands/contact` + `/api/commands/pending` | `POST` + `GET` |
| `helmet/eye/fatigue` | `/api/eye/fatigue` + `/api/commands/pending` | `POST` + `GET` |

## Firmware ESP32

Fichier: `firmwares/mqtt_system/main_esp32_controller.ino`

Changements:
- `PubSubClient` remplacé par `HTTPClient`
- Publish multiples MQTT remplacés par batch JSON `POST /api/sensors/batch`
- Callback MQTT remplacé par polling `GET /api/commands/pending`
- Ajout retry exponentiel sur erreurs réseau/serveur

Configuration:
- `firmwares/mqtt_system/http_config.h`

## Backend Python

Fichier: `servers/mqtt_server.py` (nom conservé)

Changements:
- suppression du client MQTT
- ajout endpoints REST pour ingestion capteurs/alertes
- ajout endpoints commandes (`pending`, `reset`, `threshold`, `contact`)
- conservation de l'émission Socket.IO (`sensor_update`)

Entrypoint:
- `servers/http_server.py`

## Équivalence de comportement

- **Format des données**: JSON conservé
- **Alertes**: logique inchangée (crash, alcool, fréquence cardiaque, fatigue)
- **Reset**: commande one-shot conservée
- **Dashboard**: flux en temps réel conservé via Socket.IO

## Impact performance

- HTTP ajoute un coût par requête, compensé par le **batching**
- Polling commandes introduit une latence dépendante de `COMMAND_POLL_INTERVAL` (2s par défaut)
- Retry exponentiel remplace la robustesse QoS MQTT côté transport

## Rollback rapide

1. Restaurer les fichiers MQTT précédents:
   - `firmwares/mqtt_system/main_esp32_controller.ino`
   - `firmwares/mqtt_system/mqtt_config.h`
   - `servers/mqtt_server.py`
2. Redémarrer le broker MQTT + serveur historique
