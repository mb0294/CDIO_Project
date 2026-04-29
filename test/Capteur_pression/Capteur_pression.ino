#define PRESSURE_PIN 35

void setup() {
  Serial.begin(115200);
}

void loop() {
  int value = analogRead(PRESSURE_PIN);
  Serial.println(value);
  delay(1000);
}