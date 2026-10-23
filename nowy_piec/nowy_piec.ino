// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Krzysztof Olek <blobyouty@gmail.com>

#include <Arduino.h>
#include <SPI.h>
#include <RTC.h>
#include <NTPClient.h>
#include <WiFiS3.h>
#include <Adafruit_GFX.h>
#include <WiFiUdp.h>
#include <Adafruit_SSD1306.h>
#include <WebSocketsServer.h>
#include <Servo.h>
#include <EEPROM.h>
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include <type_traits>

#include "config.h"
#include "secrets.h"

#define INT_MAX 2147483647

Adafruit_SSD1306 display = Adafruit_SSD1306(128, 32, &Wire1);
unsigned long displayTime = 0;
unsigned long wsTime = 0;

float oxygen = 0.0;
float lambda = 0.0;
int startTime = 0;
bool pump = true;

class Timer {
public:
  unsigned long lastCheck = 0;
  int waitTime = 0;

  void set_wait_time(int newWait) {
    this->waitTime = newWait;
    this->lastCheck = millis();
  }

  bool check_time(unsigned long time = millis()) {
    if ((this->waitTime + this->lastCheck) <= time) {
      this->lastCheck = time;
      return true;
    }

    return false;
  }
};

Timer normalTimer;
Timer overrideTimer;
Timer dataTimer;

int wifiStatus = WL_IDLE_STATUS;
WiFiUDP Udp;  // A UDP instance to let us send and receive packets over UDP
NTPClient timeClient(Udp);

WebSocketsServer server(80);

Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver(0x7F, Wire);
int servoPWM = 300;

bool wifi = false;

struct Config {
  float targetOxygen;
  float deadZone;
  float overdrive;
  float oxygenPumpCutOut;
  float raiseMulti;
  float fallMulti;
  float neutralMulti;
  float alphaThreshold;
  int maxServo;
  int minServo;
  int openStep;
  int closeStep;
  int waitTime;
  int startPosition;
  int initialWaitTime;
  int overdriveStep;
  int checkTime;
  int dataAcquire;
};

Config defaultConfig = {
  7.0,
  0.25,
  1.0,
  10.0,
  1.0,
  1.0,
  1.0,
  0.25,
  415,
  315,
  1,
  1,
  400,
  400,
  1000,
  1,
  100,
  50
};

uint32_t get_compilation_hash() {
  auto* data = reinterpret_cast<const uint8_t*>(&defaultConfig);
  uint32_t hash = 2166136261u;

  for (size_t i = 0; sizeof(defaultConfig) > i; i++) {
    hash ^= data[i];
    hash *= 16777619u;
  }

  return hash;
}



struct ConfigHolder {
  uint32_t magic = 0x69420;
  uint32_t hash;
  Config config;
};

ConfigHolder configHolder;

class Regression {
public:
  struct RegressionResult {
    float alpha = 0.0;
    float beta = 0.0;
  };

  static constexpr uint8_t BUFFER_SIZE = 20;
  float data_x[BUFFER_SIZE] = {0};
  float data_y[BUFFER_SIZE] = {0};

  uint8_t count = 0;
  uint8_t head = 0;

  RegressionResult lastResult {};

  struct Avg {
    float x = 0.0;
    float y = 0.0;
  };

  Avg calculateAverage() const {
    float sum_x = 0.0;
    float sum_y = 0.0;
    for (uint8_t i = 0; i < count; i++) {
      sum_x += data_x[i];
      sum_y += data_y[i];
    }
    return Avg { sum_x / count, sum_y / count };
  }

  Regression() = default;

  void updateData(float oxygen) {
    data_x[head] = millis() / 1000.0f;
    data_y[head] = oxygen;

    head = (head + 1) % BUFFER_SIZE;
    if (count < BUFFER_SIZE) {
      count++;
    }
  }

  bool isReady() const {
    return count >= BUFFER_SIZE;
  }

  RegressionResult calculateLinearRegression() {
    if (!isReady()) {
      return lastResult;
    }

    Avg avg = calculateAverage();

    float cov = 0.0;
    float var = 0.0;

    for (uint8_t i = 0; i < BUFFER_SIZE; i++) {
      float dx = data_x[i] - avg.x;
      float dy = data_y[i] - avg.y;
      cov += dx * dy;
      var += dx * dx;
    }

    if (var > 0.00001f) {
      lastResult.alpha = cov / var;
      lastResult.beta = avg.y - (lastResult.alpha * avg.x);
    }

    return lastResult;
  }
};

Regression regression;

void printOnDisplay(const char* line1, const char* line2) {
  display.clearDisplay();
  display.setCursor(0, 0);

  display.println(line1);
  display.println(line2);

  display.setCursor(0, 0);
  display.display();
}

void waitDisplay(const char* line, int d) {
  int progress = d;
  while (progress > 34) {
    String s = String(progress) + "/" + String(d) + " ms";
    printOnDisplay(line, String(progress).c_str());
    progress -= 34;
    delay(34);
  }
  delay(progress);
}

void connectToWiFi() {
  printOnDisplay("WiFi", "Prosze czekac");

  // check for the WiFi module:
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("Communication with WiFi module failed!");
    waitDisplay("Blad WiFi", 5000);
    return;
  }

  // attempt to connect to WiFi network:
  while (wifiStatus != WL_CONNECTED) {
    Serial.print("Attempting to connect to SSID: ");
    Serial.println(SSID);
    // Connect to WPA/WPA2 network. Change this line if using open or WEP network:
    wifiStatus = WiFi.begin(SSID, PASSWORD);
    // wait 10 seconds for connection:
    waitDisplay("WiFi", 10000);

    pinMode(BUTTON_PIN, OUTPUT);
    digitalWrite(BUTTON_PIN, HIGH);
    delay(5);
    pinMode(BUTTON_PIN, INPUT);
    bool buttonState = digitalRead(BUTTON_PIN);

    if (buttonState == 0) {
      waitDisplay("Wyl. WiFi", 2000);
      delay(2000);

      wifi = false;
      return;
    }
  }

  wifi = true;
  Serial.print("Connected to WiFi ");
  Serial.println(WiFi.localIP());
}

void setup() {
  //Set up serial communication.
  Serial.begin(9600);

  pinMode(PUMPS_SSR_PIN, OUTPUT);
  EEPROM.get(0, configHolder);

  uint32_t hash = get_compilation_hash();

  if (configHolder.magic != 0x69420 || configHolder.hash != hash) {
    configHolder.magic = 0x69420;
    configHolder.hash = hash;
    configHolder.config = defaultConfig;
    EEPROM.put(0, configHolder);
  }

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // Address 0x3C for 128x32
  display.display();
  delay(100);
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("LINE 1");
  display.println("LINE 2");

  display.setCursor(0, 0);
  display.display();


  connectToWiFi();

  printOnDisplay("Pob. czas.", "");
  RTC.begin();
  if (wifi) {
    Serial.println("\nStarting connection to server...");
    timeClient.begin();
    timeClient.update();
    auto unixTime = timeClient.getEpochTime() + (2 * 3600);
    Serial.print("Unix time = ");
    Serial.println(unixTime);
    RTCTime timeToSet = RTCTime(unixTime);
    RTC.setTime(timeToSet);
  } else {
    auto time = RTCTime(7, Month::JUNE, 2023, 13, 03, 00, DayOfWeek::WEDNESDAY, SaveLight::SAVING_TIME_ACTIVE);
    RTC.setTime(time);
  }

  if (wifi) {
    server.begin();
    server.onEvent(webSocketEvent);
  }

  pwm.begin();
  pwm.setPWMFreq(50);
  pwm.setPWM(0, 0, configHolder.config.startPosition);

  cj125Init();
  start();

  Serial.println("WERSJA 1");

  Timer normalTimer;
  Timer overrideTimer;
  Timer dataTimer;

  normalTimer.set_wait_time(configHolder.config.waitTime);
  overrideTimer.set_wait_time(configHolder.config.checkTime);
  dataTimer.set_wait_time(configHolder.config.dataAcquire);

  startTime = millis();
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      {
        //Serial.print(num);
        //Serial.println(" WebSocket client disconnect");
        break;
      }
    case WStype_CONNECTED:
      {
        Serial.print("WebSocket Connect!");
        server.sendTXT(num, "Connected");
      }
      break;
    case WStype_TEXT:
      Serial.print(num);
      Serial.print(" get Text: ");
      Serial.println((char*)payload);
      getData(payload);

      // send message to client
      // webSocket.sendTXT(num, "message here");

      // send data to all connected clients
      // webSocket.broadcastTXT("message here");
      break;
    case WStype_BIN:
      Serial.print("get binary length: %u\n");

      // send message to client
      // webSocket.sendBIN(num, payload, length);
      break;
    case WStype_ERROR:
    case WStype_FRAGMENT_TEXT_START:
    case WStype_FRAGMENT_BIN_START:
    case WStype_FRAGMENT:
    case WStype_FRAGMENT_FIN:
      Serial.print("WebSocket Error");
      break;
  }
}

class DataDescriptionBase {
public:
  virtual ~DataDescriptionBase() = default;

  virtual void update(const String&) = 0;
  virtual String get_json() = 0;
};

template<typename T>
class DataDescription : public DataDescriptionBase {
public:
  const char* cmd;
  const char* name;
  T* ptr;
  T min;
  T max;

  DataDescription(const char* cmd, const char* name, T* ptr, T min, T max)
  : cmd(cmd), name(name), ptr(ptr), min(min), max(max) {}
  ~DataDescription() = default;

  void update(const String& str) {
    // Command always follows pattern: "cmd value"

    if (!str.startsWith(this->cmd)) return;

    const uint8_t offset = strlen(this->cmd);
    if (offset + 1 >= str.length() || str[offset] != ' ') return;

    T value{};

    if constexpr (std::is_same_v<T, float>)    value = str.substring(offset + 1).toFloat();
    else if constexpr (std::is_same_v<T, int>) value = str.substring(offset + 1).toInt();

    if (value < this->min || value > this->max) return;

    *(this->ptr) = value;

    Serial.print("Update ");
    Serial.print(this->name);
    Serial.print(": ");
    Serial.println(value);

    EEPROM.put(0, configHolder);
  }

  String get_json() {
    String str = ", \"";
    str += String(this->name);
    str += "\": ";
    str += String(*(this->ptr));

    return str;
  }
};

//floats
DataDescription<float> descTO("TO", "Target Oxygen", &configHolder.config.targetOxygen, 0.0f, 25.0f);
DataDescription<float> descDZ("DZ", "Dead Zone", &configHolder.config.deadZone, 0.0f, 25.0f);
DataDescription<float> descOv("Ov", "Overdrive", &configHolder.config.overdrive, 0.0f, 25.0f);
DataDescription<float> descOP("OP", "Oxygen Pump Cut Out", &configHolder.config.oxygenPumpCutOut, 0.0f, 25.0f);
DataDescription<float> descRM("RM", "Raise Multi", &configHolder.config.raiseMulti, 0.0f, 1000.0f);
DataDescription<float> descFM("FM", "Fall Multi", &configHolder.config.fallMulti, 0.0f, 1000.0f);
DataDescription<float> descNM("NM", "Neutral Multi", &configHolder.config.neutralMulti, 0.0f, 1000.0f);
DataDescription<float> descAT("AT", "Alpha Threshold", &configHolder.config.alphaThreshold, 0.0f, 1000.0f);

//integers
DataDescription<int> descMS("MS", "Max Servo", &configHolder.config.maxServo, 0, 4096);
DataDescription<int> descMi("Mi", "Min Servo", &configHolder.config.minServo, 0, 4096);
DataDescription<int> descOS("OS", "Open Step", &configHolder.config.openStep, 0, 4096);
DataDescription<int> descCS("CS", "Close Step", &configHolder.config.closeStep, 0, 4096);
DataDescription<int> descWT("WT", "Wait Time", &configHolder.config.waitTime, 0, INT_MAX);
DataDescription<int> descSP("SP", "Start Position", &configHolder.config.startPosition, 0, 4096);
DataDescription<int> descIW("IW", "Initial Wait Time", &configHolder.config.initialWaitTime, 0, INT_MAX);
DataDescription<int> descVS("VS", "Overdrive Step", &configHolder.config.overdriveStep, 0, 4096);
DataDescription<int> descCT("CT", "Check Time", &configHolder.config.checkTime, 0, INT_MAX);
DataDescription<int> descDA("DA", "Data Acquire", &configHolder.config.dataAcquire, 0, INT_MAX);

DataDescriptionBase* desc[] {
  &descTO, &descDZ, &descOv, &descOP, &descRM, &descFM, &descNM, &descAT,
  &descMS, &descMi, &descOS, &descCS, &descWT, &descSP, &descIW, &descVS, &descCT, &descDA
};

void sendData() {
  String str = "{ \"time\": \"";

  str += String(millis());

  str += "\", \"oxygen\": ";
  str += String(oxygen);

  str += ", \"servo\": ";
  str += String(servoPWM);

  for (auto d : desc) str += d->get_json();

  str += ", \"alpha\": ";
  str += String(regression.lastResult.alpha);

  str += ", \"beta\": ";
  str += String(regression.lastResult.beta);

  str += " }";

  server.broadcastTXT(str);
}


void getData(uint8_t* payload) {
  auto str = String((char*)payload);

  for (auto d : desc) d->update(str);

  normalTimer.set_wait_time(configHolder.config.waitTime);
  overrideTimer.set_wait_time(configHolder.config.checkTime);
  dataTimer.set_wait_time(configHolder.config.dataAcquire);
}

//Infinite loop.
void loop() {
  auto time = millis();

  //detect time overflow
  if (time < wsTime) {
    wsTime = time;
    displayTime = time;
    normalTimer.set_wait_time(configHolder.config.waitTime);
    overrideTimer.set_wait_time(configHolder.config.checkTime);
    dataTimer.set_wait_time(configHolder.config.dataAcquire);
    startTime = 0;
  }

  cj125Update();

  if (WEBSOCKET_COOLDOWN + wsTime <= time) {
    if (wifi) {
      server.loop();
      sendData();

      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi not connected");
      }
    }

    wsTime = time;
  }

  pinMode(BUTTON_PIN, OUTPUT);
  digitalWrite(BUTTON_PIN, HIGH);
  delay(5);
  pinMode(BUTTON_PIN, INPUT);
  bool buttonState = digitalRead(BUTTON_PIN);

  float diff = oxygen - configHolder.config.targetOxygen;
  int temp_angle = servoPWM;

  if (startTime + configHolder.config.dataAcquire)

  if (dataTimer.check_time()) {
    regression.updateData(oxygen);
    regression.calculateLinearRegression();
  }

  auto reg = regression.lastResult;

  bool bypass = abs(reg.alpha) > configHolder.config.alphaThreshold;

  if (DISPLAY_COOLDOWN + displayTime <= time) {
    String status = "Z: ";
    status += String(configHolder.config.targetOxygen);

    String status2 = "O: ";
    status2 += String(oxygen);

    printOnDisplay(status.c_str(), status2.c_str());

    displayTime = time;

    if (buttonState == 0) {
      if (configHolder.config.targetOxygen < 25.0) {
        configHolder.config.targetOxygen += 0.25;
      } else if (configHolder.config.targetOxygen >= 25) {
        configHolder.config.targetOxygen = 0;
      }
    }
  }

  if (startTime + configHolder.config.initialWaitTime > time) return;

  if ((oxygen > configHolder.config.oxygenPumpCutOut && pump)) {
    pump = false;
    pinMode(PUMPS_SSR_PIN, OUTPUT);
    digitalWrite(PUMPS_SSR_PIN, LOW); 
  }

  if (normalTimer.check_time() || bypass) {
    float mul = configHolder.config.neutralMulti;

    if (diff > configHolder.config.deadZone) {
      if (configHolder.config.alphaThreshold < reg.alpha) mul = configHolder.config.raiseMulti;
      if (-configHolder.config.alphaThreshold > reg.alpha) mul = configHolder.config.fallMulti;

      temp_angle += floor(float(configHolder.config.openStep) * mul);
    }
    if (diff < -configHolder.config.deadZone) {
      if (configHolder.config.alphaThreshold < reg.alpha) mul = configHolder.config.fallMulti;
      if (-configHolder.config.alphaThreshold > reg.alpha) mul = configHolder.config.raiseMulti;

      temp_angle -= ceil(float(configHolder.config.closeStep) * mul);
    }
  }


  if (overrideTimer.check_time()) {
    float mul = configHolder.config.neutralMulti;

    if (configHolder.config.alphaThreshold < reg.alpha) mul = configHolder.config.raiseMulti;
    if (-configHolder.config.alphaThreshold > reg.alpha) mul = configHolder.config.fallMulti;

    if (diff > configHolder.config.overdrive) {
      if (configHolder.config.alphaThreshold < reg.alpha) mul = configHolder.config.raiseMulti;
      if (-configHolder.config.alphaThreshold > reg.alpha) mul = configHolder.config.fallMulti;

      temp_angle += floor(float(configHolder.config.overdriveStep) * mul);
    }
    if (diff < -configHolder.config.overdrive) {
      if (configHolder.config.alphaThreshold < reg.alpha) mul = configHolder.config.fallMulti;
      if (-configHolder.config.alphaThreshold > reg.alpha) mul = configHolder.config.raiseMulti;

      temp_angle -= ceil(float(configHolder.config.overdriveStep) * mul);
    }
  }

  if (temp_angle != servoPWM) {
    temp_angle = constrain(temp_angle, configHolder.config.minServo, configHolder.config.maxServo);

    servoPWM = temp_angle;
    pwm.setPWM(0, 0, temp_angle);
  }
}
