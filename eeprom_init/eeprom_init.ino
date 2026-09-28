#include <EEPROM.h>

struct Config {
  float targetOxygen;
  float deadZone;
  float overdrive;
  int maxServo;
  int minServo;
  int openStep;
  int closeStep;
  int waitTime;
  int startPosition;
  int initialWaitTime;
  int overdriveStep;
  int checkTime;
};

Config config = {
  7.0,
  0.25,
  1.0,
  600,
  150,
  1,
  1,
  400,
  400,
  1000,
  1,
  100
};

void setup() {
  Serial.begin(9600);
  EEPROM.put(0, config);
  Serial.println("Done!");
}

void loop() {
  while (true) {}
}
