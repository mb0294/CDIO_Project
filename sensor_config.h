/**
 * sensor_config.h
 * Pin definitions and alert thresholds for Smart Helmet System
 */

#ifndef SENSOR_CONFIG_H
#define SENSOR_CONFIG_H

// ===========================================================================
// ESP32 – Pin Definitions
// ===========================================================================

// GPS (NEO-6M) – UART1
#define GPS_RX_PIN   16
#define GPS_TX_PIN   17
#define GPS_BAUD     9600

// Heart Rate / Oxygen (MAX30102) – I2C
#define I2C_SDA      21
#define I2C_SCL      22

// Gyroscope (MPU-9250) – I2C (same bus, address 0x68)
#define MPU_I2C_ADDR 0x68

// Alcohol sensor (MQ-3) – Analog input
#define ALCOHOL_PIN  34   // ADC1_CH6 (input only)

// Pressure sensor – Analog input
#define PRESSURE_PIN 35   // ADC1_CH7 (input only)

// SIM800L – UART2
#define SIM800_RX_PIN 26
#define SIM800_TX_PIN 27
#define SIM800_BAUD   9600

// Buzzer / vibration motor
#define BUZZER_PIN   25

// Status LED
#define LED_PIN      2    // Onboard LED on most ESP32 boards

// ===========================================================================
// ESP32-CAM – Pin Definitions (AI-Thinker module)
// ===========================================================================
// Camera pins are defined by the AI-Thinker camera model in esp_camera.h;
// the constants below are used for auxiliary features only.
#define CAM_LED_PIN  4    // Onboard flash LED
#define CAM_RX_PIN  14   // UART to main ESP32 (optional backup channel)
#define CAM_TX_PIN  15

// ===========================================================================
// Alert Thresholds
// ===========================================================================

// Heart rate (BPM)
#define HEART_RATE_MAX   120
#define HEART_RATE_MIN    50

// Oxygen saturation (%SpO2)
#define SPO2_MIN          90

// Alcohol (0–4095 raw ADC, MQ-3 sensor)
#define ALCOHOL_THRESHOLD 2000

// Lean / tilt angle (degrees) – MPU-9250 roll axis
#define LEAN_ANGLE_MAX    45.0f

// Crash detection – instantaneous acceleration delta (g)
#define CRASH_ACCEL_THRESHOLD  3.0f   // > 3 g spike = crash

// Eye Aspect Ratio (EAR) – eye closure
#define EAR_THRESHOLD          0.20f  // Below this = eye closed
#define EYE_CLOSURE_MAX_SEC    2.0f   // Closed > 2 s = drowsy alert

// Blink rate (blinks / minute)
#define BLINK_RATE_MIN         10     // Below this = fatigue warning

// Fatigue consecutive frame count
#define FATIGUE_CONSEC_FRAMES  20     // 20 frames below EAR threshold

// ===========================================================================
// Emergency Contacts  (SIM800L)
// ===========================================================================
#define EMERGENCY_CONTACT_1  "+33600000000"   // Replace with real numbers
#define EMERGENCY_CONTACT_2  "+33600000001"

// ===========================================================================
// Timing
// ===========================================================================
#define SMS_COOLDOWN_MS        30000  // Minimum 30 s between SMS alerts
#define BUZZER_DURATION_MS      2000  // Buzzer on duration
#define SENSOR_READ_INTERVAL_MS 1000  // Main sensor polling interval

#endif // SENSOR_CONFIG_H
