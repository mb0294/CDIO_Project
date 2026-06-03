# 🪖 Smart Helmet - Guide d'Installation et Exécution

## 📋 Table des Matières
1. [Matériel Requis](#matériel-requis)
2. [Câblage ESP32-CAM](#câblage-esp32-cam)
3. [Câblage ESP32-S Capteurs](#câblage-esp32-s-capteurs)
4. [Upload Firmware](#upload-firmware)
5. [Exécution Code Python](#exécution-code-python)
6. [Accès Dashboard](#accès-dashboard)

---

## 🛠️ Matériel Requis

### Pour ESP32-CAM (Système HTTP - Recommandé)
- ESP32-CAM (AI Thinker)
- Module USB-TTL (CP2102 ou CH340)
- LED (optionnel pour alertes)
- Alimentation 5V 2A minimum

### Pour ESP32-S (Système MQTT - Capteurs)
- ESP32 Dev Module
- GPS Module (NEO-6M)
- Capteur fréquence cardiaque/SpO2 (MAX30102)
- Capteur alcool (MQ-3)
- Capteur pression/altitude (BMP280)
- Accéléromètre/Gyro (MPU-9250)
- Module GSM (SIM800L)
- Fils de connexion (jumper wires)
- Breadboard

---

## 🔌 Câblage ESP32-CAM

### ESP32-CAM ↔ USB-TTL (Pour upload)

| ESP32-CAM | USB-TTL | Description |
|-----------|---------|-------------|
| VCC | 5V | Alimentation |
| GND | GND | Ground |
| RX | TX | Réception |
| TX | RX | Transmission |
| IO0 | GND | Mode téléchargement |
| RESET | - | Reset (optionnel) |

**⚠️ IMPORTANT:**
- Connecter IO0 à GND pendant l'upload
- Déconnecter IO0 après l'upload pour le mode normal
- Utiliser une alimentation 5V externe pour le ESP32-CAM (le USB-TTL ne fournit pas assez de courant)

### ESP32-CAM ↔ LED (Optionnel)

| ESP32-CAM | LED |
|-----------|-----|
| GPIO 14 | Anode (+) |
| GND | Cathode (-) via résistance 220Ω |

---

## 🔌 Câblage ESP32-CAM Capteurs

**NOTE:** ESP32-CAM (AI Thinker) a des GPIO limités. Pins disponibles: 0, 1, 2, 3, 4, 12, 13, 14, 15, 16

### ESP32-CAM ↔ GPS (NEO-6M)

| ESP32-CAM | GPS |
|-----------|-----|
| 3.3V | VCC |
| GND | GND |
| GPIO 15 | RX |
| GPIO 14 | TX |

### ESP32-CAM ↔ MAX30102 (Cœur/SpO2)

| ESP32-CAM | MAX30102 |
|-----------|-----------|
| 3.3V | VIN |
| GND | GND |
| GPIO 12 (SDA) | SDA |
| GPIO 13 (SCL) | SCL |

### ESP32-CAM ↔ MQ-3 (Alcool)

| ESP32-CAM | MQ-3 |
|-----------|-------|
| 5V | VCC |
| GND | GND |
| GPIO 16 | AOUT |

### ESP32-CAM ↔ BMP280 (Pression/Altitude)

| ESP32-CAM | BMP280 |
|-----------|--------|
| 3.3V | VCC |
| GND | GND |
| GPIO 12 (SDA) | SDA |
| GPIO 13 (SCL) | SCL |

### ESP32-CAM ↔ MPU-9250 (Accéléromètre)

| ESP32-CAM | MPU-9250 |
|-----------|----------|
| 3.3V | VCC |
| GND | GND |
| GPIO 12 (SDA) | SDA |
| GPIO 13 (SCL) | SCL |

### ESP32-CAM ↔ Buzzer (Alertes)

| ESP32-CAM | Buzzer |
|-----------|--------|
| GPIO 4 | Anode (+) |
| GND | Cathode (-) via résistance 220Ω |

---

## � Upload Firmware

### Étape 1: Installation Arduino IDE

1. Télécharger Arduino IDE: https://www.arduino.cc/en/software
2. Installer le package ESP32:
   - Ouvrir Arduino IDE
   - Fichier → Préférences
   - URL de gestionnaire de cartes supplémentaires:
     ```
     https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
     ```
   - Outils → Carte → Gestionnaire de cartes
   - Rechercher "esp32" et installer "esp32 by Espressif Systems"

### Étape 2: Upload ESP32-CAM (Système HTTP)

1. **Ouvrir le firmware:**
   ```
   c:\Users\mehdi\CDIO_Project\firmwares\http_system\eye.ino
   ```

2. **Configurer WiFi:**
   - Modifier lignes 22-23:
   ```cpp
   const char* ssid = "VOTRE_WIFI";
   const char* password = "VOTRE_MOT_DE_PASSE";
   ```

3. **Configuration Arduino IDE:**
   - Outils → Carte → ESP32 Arduino → AI Thinker ESP32-CAM
   - Outils → Partition Scheme → Huge APP (3MB No OTA/1MB SPIFFS)
   - Outils → CPU Frequency → 240MHz
   - Outils → Flash Frequency → 40MHz
   - Outils → Flash Mode → QIO
   - Outils → Upload Speed → 921600

4. **Connecter ESP32-CAM:**
   - Connecter IO0 à GND
   - Connecter USB-TTL
   - Alimenter avec 5V externe

5. **Upload:**
   - Cliquer sur le bouton Upload
   - Attendre la fin du transfert
   - Déconnecter IO0 de GND
   - Appuyer sur le bouton RESET

### Étape 3: Upload ESP32-S (Système MQTT - Optionnel)

1. **Ouvrir le firmware:**
   ```
   c:\Users\mehdi\CDIO_Project\firmwares\mqtt_system\main_esp32_controller.ino
   ```

2. **Configurer capteurs:**
   - Ouvrir `sensor_config.h`
   - Configurer les pins selon votre câblage

3. **Configurer WiFi:**
   - Ouvrir `mqtt_config.h`
   - Modifier lignes 12-13:
   ```cpp
   #define WIFI_SSID     "YOUR_WIFI_SSID"
   #define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
   ```

4. **Configuration Arduino IDE:**
   - Outils → Carte → ESP32 Arduino → ESP32 Dev Module
   - Outils → CPU Frequency → 240MHz
   - Outils → Flash Size → 4MB

5. **Upload:**
   - Connecter ESP32-S via USB
   - Cliquer sur Upload

---

## 🐍 Exécution Code Python

### Étape 1: Installation Python

1. Vérifier Python 3.11+ installé:
   ```bash
   python --version
   ```

2. Créer environnement virtuel (optionnel):
   ```bash
   python -m venv venv
   venv\Scripts\activate
   ```

### Étape 2: Installation Dépendances

**Pour système HTTP (Recommandé):**
```bash
pip install -r requirements/requirements_clean.txt
```

**Pour système MQTT:**
```bash
pip install -r requirements/requirements_mqtt.txt
```

### Étape 3: Lancement Détecteur HTTP

1. **Trouver l'IP de l'ESP32-CAM:**
   - Ouvrir le Serial Monitor (115200 baud)
   - L'IP sera affichée après connexion WiFi
   - Exemple: `192.168.1.100`

2. **Lancer le détecteur:**
   ```bash
   python servers/http_detector.py --esp32 http://192.168.1.100
   ```

3. **Paramètres optionnels:**
   ```bash
   python servers/http_detector.py \
       --esp32 http://192.168.1.100 \
       --ear-thresh 0.15 \
       --mar-thresh 0.6 \
       --consec-frames 20 \
       --yawn-frames 15
   ```

### Étape 4: Lancement Serveur MQTT (Optionnel)

```bash
python servers/mqtt_server.py
```

---

## 🌐 Accès Dashboard

### Dashboard HTTP (Système HTTP)

1. **Ouvrir le navigateur:**
   ```
   http://localhost:5000
   ```

2. **Fonctionnalités:**
   - Vidéo en direct
   - Statistiques EAR/MAR
   - Alertes visuelles
   - Snapshots automatiques

### Dashboard MQTT (Système MQTT)

1. **Ouvrir le fichier:**
   ```
   c:\Users\mehdi\CDIO_Project\servers\mqtt_dashboard.html
   ```

2. **Configuration:**
   - Broker: `broker.hivemq.com`
   - Port: `8000` (WebSockets)
   - Path: `/mqtt`
   - URL Vidéo: `http://192.168.1.100/stream`

3. **Cliquer sur "Se connecter"**

4. **Données affichées:**
   - État des yeux
   - Fréquence cardiaque
   - SpO2
   - Alcool
   - GPS
   - Pression
   - Altitude
   - Angle d'inclinaison
   - Détection crash

---

## ✅ Vérification

### ESP32-CAM
- ✅ LED WiFi clignote
- ✅ Serial Monitor affiche l'IP
- ✅ Stream accessible: `http://IP_ESP/stream`

### Python
- ✅ Aucune erreur au démarrage
- ✅ Dashboard accessible
- ✅ Détection fonctionnelle

### Dashboard MQTT
- ✅ Statut "Connecté"
- ✅ Données reçues en temps réel

---

## 🐛 Dépannage

### ESP32-CAM ne se connecte pas au WiFi
- Vérifier SSID et mot de passe
- Vérifier alimentation 5V
- Réessayer l'upload

### Stream vidéo inaccessible
- Vérifier l'IP dans Serial Monitor
- Vérifier la connexion réseau
- Tester avec: `http://IP_ESP/stream`

### Python ne détecte pas
- Vérifier l'URL ESP32
- Vérifier les dépendances installées
- Vérifier le Serial Monitor pour erreurs

### Dashboard MQTT ne se connecte pas
- Vérifier broker: `broker.hivemq.com`
- Vérifier port: `8000` (pas `1883`)
- Vérifier connexion internet

---

## 📞 Support

Pour plus d'informations, consultez le README.md principal.
