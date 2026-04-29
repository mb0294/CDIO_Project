#include <HardwareSerial.h>
HardwareSerial sim800(1);

void setup() {
  Serial.begin(115200);
  sim800.begin(9600, SERIAL_8N1, 26, 27);
}

void loop() {
  sim800.println("AT");
  delay(1000);
  while(sim800.available()){
    Serial.write(sim800.read());
  }
}