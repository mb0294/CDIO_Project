#define MQ3_PIN 34
#define LED_PIN 25

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
}

void loop() {
  int alcohol = analogRead(MQ3_PIN);

  Serial.println(alcohol);

  if (alcohol > 2000) {
    digitalWrite(LED_PIN, HIGH);
    Serial.println("ALCOOL DETECTE !");
  } else {
    digitalWrite(LED_PIN, LOW);
  }

  delay(1000);
}