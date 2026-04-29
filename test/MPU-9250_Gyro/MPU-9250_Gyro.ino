#include <Wire.h>
#include <MPU9250.h>

MPU9250 mpu;

void setup() {
  Serial.begin(115200);
  Wire.begin();
  mpu.setup(0x68);
}

void loop() {
  mpu.update();
  Serial.print("AccX: ");
  Serial.println(mpu.getAccX());
  delay(500);
}