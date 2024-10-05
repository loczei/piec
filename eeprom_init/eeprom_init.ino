#include <EEPROM.h>

struct Config {
  float targetOxygen;
  float oxygenPumpCutOut;
};

Config config = {
  7.0,
  10.0
};

void setup() {
  Serial.begin(9600);
  EEPROM.put(0, config);
  Serial.println("Done!");
}

void loop() {
  while (true) {}
}