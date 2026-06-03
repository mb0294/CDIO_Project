#include <Wire.h>
#include "MAX30105.h"

MAX30105 particleSensor;

void setup() {
  Serial.begin(115200);
  Wire.begin();
  if (!particleSensor.begin(Wire)) {
    Serial.println("MAX30102 non détecté");
    while (1);
  }
}

void loop() {
  long irValue = particleSensor.getIR();
  Serial.println(irValue);
  delay(500);
}