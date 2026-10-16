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

#include "config.h"
#include "secrets.h"

Adafruit_SSD1306 display = Adafruit_SSD1306(128, 32, &Wire1);
unsigned long displayTime = 0;
unsigned long wsTime = 0;

float oxygen = 0.0;
float lambda = 0.0;
unsigned long lastCheck = 0;
unsigned long lastOverrideCheck = 0;
int startTime = 0;
bool pump = true;

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
};

Config config;

class Regression {
public:
  struct RegressionResult {
    float alpha = 0.0;
    float beta = 0.0;
  };

  static constexpr uint8_t BUFFER_SIZE = 10;
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

public:
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
  EEPROM.get(0, config);

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
  pwm.setPWM(0, 0, config.startPosition);

  cj125Init();
  start();

  Serial.println("WERSJA 1");

  lastCheck = lastOverrideCheck = startTime = millis();
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

void sendData() {
  String str = "{ \"time\": \"";

  str += String(millis());

  str += "\", \"oxygen\": ";
  str += String(oxygen);

  str += ", \"target\": ";
  str += String(config.targetOxygen);

  str += ", \"servo\": ";
  str += String(servoPWM);

  str += ", \"deadZone\": ";
  str += String(config.deadZone);

  str += ", \"overdrive\": ";
  str += String(config.overdrive);

  str += ", \"maxServo\": ";
  str += String(config.maxServo);

  str += ", \"minServo\": ";
  str += String(config.minServo);

  str += ", \"openStep\": ";
  str += String(config.openStep);

  str += ", \"closeStep\": ";
  str += String(config.closeStep);

  str += ", \"closeStep\": ";
  str += String(config.closeStep);

  str += ", \"waitTime\": ";
  str += String(config.waitTime);

  str += ", \"startPosition\": ";
  str += String(config.startPosition);

  str += ", \"initialWaitTime\": ";
  str += String(config.initialWaitTime);

  str += ", \"overdriveStep\": ";
  str += String(config.overdriveStep);

  str += ", \"checkTime\": ";
  str += String(config.checkTime);

  str += ", \"oxygenPumpCutOut\": ";
  str += String(config.oxygenPumpCutOut);

  str += ", \"raiseMulti\": ";
  str += String(config.raiseMulti);

  str += ", \"fallMulti\": ";
  str += String(config.fallMulti);

  str += ", \"neutralMulti\": ";
  str += String(config.neutralMulti);

  str += ", \"alphaThreshold\": ";
  str += String(config.alphaThreshold);

  str += ", \"alpha\": ";
  str += String(regression.lastResult.alpha);

  str += ", \"beta\": ";
  str += String(regression.lastResult.beta);

  str += " }";

  server.broadcastTXT(str);
}

void getData(uint8_t* payload) {
  auto str = String((char*)payload);

  switch (str[0]) {
    case 'T': {
      float temp = str.substring(2, str.length()).toFloat();
      if (temp < 0.00) return;

      config.targetOxygen = temp;
      Serial.print("New target oxygen: ");
      Serial.println(config.targetOxygen);
      EEPROM.put(0, config);
    } break;

    case 'd': {
      float temp = str.substring(2, str.length()).toFloat();
      if (temp < 0.00) return;

      config.deadZone = temp;
      Serial.print("New deadzone: ");
      Serial.println(config.deadZone);

    } break;

    case 'o': {
      float temp = str.substring(2, str.length()).toFloat();
      if (temp < 0.00) return;

      config.overdrive = temp;
      Serial.print("New overdrive: ");
      Serial.println(config.overdrive);

    } break;

    case 'S': {
      int temp = str.substring(2, str.length()).toInt();
      if (temp < 0 || temp > 4096) return;

      config.maxServo = temp;
      Serial.print("New maxServo: ");
      Serial.println(config.maxServo);

    } break;

    case 's': {
      int temp = str.substring(2, str.length()).toInt();
      if (temp < 0 || temp > 4096) return;

      config.minServo = temp;
      Serial.print("New minServo: ");
      Serial.println(config.minServo);

    } break;

    case 't': {
      int temp = str.substring(2, str.length()).toInt();
      if (temp < 0 || temp > 4096) return;

      config.openStep = temp;
      Serial.print("New openStep: ");
      Serial.println(config.openStep);

    } break;

    case 'c': {
      int temp = str.substring(2, str.length()).toInt();
      if (temp < 0 || temp > 4096) return;

      config.closeStep = temp;
      Serial.print("New closeStep: ");
      Serial.println(config.closeStep);

    } break;

    case 'w': {
      int temp = str.substring(2, str.length()).toInt();
      if (temp < 50) return;

      config.waitTime = temp;
      Serial.print("New waitTime: ");
      Serial.println(config.waitTime);

    } break;

    case 'p': {
      int temp = str.substring(2, str.length()).toInt();
      if (temp < 0) return;

      config.startPosition = temp;
      Serial.print("New startPosition: ");
      Serial.println(config.startPosition);

    } break;

    case 'i': {
      int temp = str.substring(2, str.length()).toInt();
      if (temp < 0) return;

      config.initialWaitTime = temp;
      Serial.print("New initialWaitTime: ");
      Serial.println(config.initialWaitTime);

    } break;

    case 'e': {
      int temp = str.substring(2, str.length()).toInt();
      if (temp < 50) return;

      config.checkTime = temp;
      Serial.print("New checkTime: ");
      Serial.println(config.checkTime);

    } break;

    case 'O': {
      int temp = str.substring(2, str.length()).toInt();
      if (temp < 0) return;

      config.overdriveStep = temp;
      Serial.print("New overdriveStep: ");
      Serial.println(config.overdriveStep);

    } break;
    
    case 'P': {
      int temp = str.substring(2, str.length()).toInt();
      if (temp < 0) return;

      config.oxygenPumpCutOut = temp;
      Serial.print("New oxygenPumpCutOut: ");
      Serial.println(config.oxygenPumpCutOut);

    } break;

    case 'r': {
      int temp = str.substring(2, str.length()).toInt();
      if (temp < 0) return;

      config.raiseMulti = temp;
      Serial.print("New raiseMulti: ");
      Serial.println(config.raiseMulti);

    } break;

    case 'f': {
      int temp = str.substring(2, str.length()).toInt();
      if (temp < 0) return;

      config.fallMulti = temp;
      Serial.print("New fallMulti: ");
      Serial.println(config.fallMulti);

    } break;

    case 'n': {
      int temp = str.substring(2, str.length()).toInt();
      if (temp < 0) return;

      config.neutralMulti = temp;
      Serial.print("New neutralMulti: ");
      Serial.println(config.neutralMulti);

    } break;

    case 'a': {
      int temp = str.substring(2, str.length()).toInt();
      if (temp < 0) return;

      config.alphaThreshold = temp;
      Serial.print("New alphaThreshold: ");
      Serial.println(config.alphaThreshold);

    } break;

  }

  EEPROM.put(0, config);
}

//Infinite loop.
void loop() {
  auto time = millis();

  //detect time overflow
  if (time < wsTime) {
    wsTime = time;
    displayTime = time;
    lastCheck = time;
    lastOverrideCheck = time;
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

  float diff = oxygen - config.targetOxygen;
  int temp_angle = servoPWM;

  regression.updateData(oxygen);
  auto reg = regression.calculateLinearRegression();

  bool bypass = abs(reg.alpha) > config.alphaThreshold;

  if (DISPLAY_COOLDOWN + displayTime <= time) {
    String status = "Z: ";
    status += String(config.targetOxygen);

    String status2 = "O: ";
    status2 += String(oxygen);

    printOnDisplay(status.c_str(), status2.c_str());

    displayTime = time;

    if (buttonState == 0) {
      if (config.targetOxygen < 25.0) {
        config.targetOxygen += 0.25;
      } else if (config.targetOxygen >= 25) {
        config.targetOxygen = 0;
      }
    }
  }

  if (startTime + config.initialWaitTime > time) return;

  if ((oxygen > config.oxygenPumpCutOut && pump)) {
    pump = false;
    pinMode(PUMPS_SSR_PIN, OUTPUT);
    digitalWrite(PUMPS_SSR_PIN, LOW); 
  }

  if (config.waitTime + lastCheck <= time || bypass) {
    float mul = config.neutralMulti;

    if (diff > config.deadZone) {
      if (config.alphaThreshold < reg.alpha) mul = config.raiseMulti;
      if (-config.alphaThreshold > reg.alpha) mul = config.fallMulti;

      temp_angle += floor(float(config.openStep) * mul);
    }
    if (diff < -config.deadZone) {
      if (config.alphaThreshold < reg.alpha) mul = config.fallMulti;
      if (-config.alphaThreshold > reg.alpha) mul = config.raiseMulti;

      temp_angle -= ceil(float(config.closeStep) * mul);
    }

    lastCheck = time;
  }


  if (config.checkTime + lastOverrideCheck <= time) {
    float mul = config.neutralMulti;

    if (config.alphaThreshold < reg.alpha) mul = config.raiseMulti;
    if (-config.alphaThreshold > reg.alpha) mul = config.fallMulti;

    if (diff > config.overdrive) {
      if (config.alphaThreshold < reg.alpha) mul = config.raiseMulti;
      if (-config.alphaThreshold > reg.alpha) mul = config.fallMulti;

      temp_angle += floor(float(config.overdriveStep) * mul);
    }
    if (diff < -config.overdrive) {
      if (config.alphaThreshold < reg.alpha) mul = config.fallMulti;
      if (-config.alphaThreshold > reg.alpha) mul = config.raiseMulti;

      temp_angle -= ceil(float(config.overdriveStep) * mul);
    }

    lastOverrideCheck = time;
  }

  if (temp_angle != servoPWM) {
    temp_angle = constrain(temp_angle, config.minServo, config.maxServo);

    servoPWM = temp_angle;
    pwm.setPWM(0, 0, temp_angle);
  }
}
