#include <EEPROM.h>

struct Config {
  float targetOxygen;
  float oxygenPumpCutOut;
  float oxygenTopServoCutOut;
  int maxServo;
  int minServo;
  float multiReal;
  float boostReal;
  float multiMax;
  int servoBalanceCooldown;
  int balanceMulti;
  int maxBalance;
  int topCloseSpeed;
  int topOpenSpeed;
  int topMaxServo;
  int topMinServo;
};

Config config = {
  7.0,
  10.0,
  10.0,
  600,
  150,
  2.0,
  3.0,
  30.0,
  100,
  1,
  225,
  1,
  1,
  600,
  300,
};

void setup() {
  Serial.begin(9600);
  EEPROM.put(0, config);
  Serial.println("Done!");
}

void loop() {
  while (true) {}
}