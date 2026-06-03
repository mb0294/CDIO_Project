# 🪖 Guide d'installation — Système HTTP (standard)

## 1) Matériel requis
- ESP32-CAM (AI Thinker)
- Module USB-TTL (CP2102/CH340)
- Alimentation 5V 2A
- (Optionnel) LED + résistance 220Ω

## 2) Câblage ESP32-CAM pour upload
| ESP32-CAM | USB-TTL |
|---|---|
| VCC | 5V |
| GND | GND |
| RX | TX |
| TX | RX |
| IO0 | GND (pendant upload uniquement) |

Après upload: déconnecter IO0 de GND puis RESET.

## 3) Firmware ESP32-CAM
1. Ouvrir `firmwares/http_system/eye.ino`
2. Modifier le SSID et mot de passe Wi-Fi
3. Configuration Arduino IDE:
   - Board: **AI Thinker ESP32-CAM**
   - CPU: 240MHz
   - Partition Scheme: Huge APP (3MB No OTA)
4. Upload
5. Ouvrir le Serial Monitor (115200) et noter l'IP

## 4) Environnement Python
```bash
python -m venv venv
# Linux/macOS
source venv/bin/activate
# Windows
# venv\Scripts\activate

pip install -r requirements/requirements_clean.txt
```

## 5) Lancer la détection
```bash
python servers/http_detector.py --esp32 http://192.168.1.100
```

Options utiles:
```bash
python servers/http_detector.py \
  --esp32 http://192.168.1.100 \
  --ear-thresh 0.15 \
  --mar-thresh 0.6 \
  --consec-frames 20 \
  --yawn-frames 15
```

## 6) Dashboard
Ouvrir:
```text
http://localhost:5000
```

## 7) Dépannage
- **ESP32 non détecté**: pilotes USB + câblage RX/TX
- **Pas de stream**: tester `http://IP_ESP32/stream`
- **Aucune détection**: vérifier installation de `requirements_clean.txt`
- **Migration MQTT**: voir `MIGRATION_GUIDE.md`
