#include <EEPROM.h>

struct Config {
  float targetOxygen;
  float oxygenPumpCutOut;
  float oxygenTopServoCutOut;
  int maxServoAngle;
  int minServoAngle;
  float multiReal;
  float boostReal;
  float multiMax;
  int servoBalanceCooldown;
};

Config config = {
  7.0,
  10.0,
  10.0,
  135,
  45,
  2.0,
  3.0,
  30.0,
  100,
};

void setup() {
  Serial.begin(9600);
  EEPROM.put(0, config);
  Serial.println("Done!");
}

void loop() {
  while (true) {}
}