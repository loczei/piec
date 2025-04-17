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

#include "config.h"
#include "secrets.h"

Adafruit_SSD1306 display = Adafruit_SSD1306(128, 32, &Wire);
Servo servo;
Servo topServo;
unsigned long displayTime = 0;
unsigned long wsTime = 0;
unsigned long lastServoBalanceAdj = 0;
int topServoStatus = true;
int balanceCentre = 85;

float oxygen = 0.0;
float lambda = 0.0;
bool pumpStatus = true;
int servoBalance = 0;
int pumpCounter = 0;
int pumpTime = 0;
bool pump = true;

int wifiStatus = WL_IDLE_STATUS;
WiFiUDP Udp;  // A UDP instance to let us send and receive packets over UDP
NTPClient timeClient(Udp);

WebSocketsServer server(80);

Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver(0x7F, Wire);


bool wifi = false;

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
};

Config config;

void printOnDisplay(const char* line1, const char* line2) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(line1);
  display.println(line2);

  display.setCursor(0, 0);
  display.display();
}


void connectToWiFi() {
  // check for the WiFi module:
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("Communication with WiFi module failed!");
    printOnDisplay("WiFi Err", "Prosze czekac");
    delay(5000);
    return;
  }

  // attempt to connect to WiFi network:
  while (wifiStatus != WL_CONNECTED) {
    Serial.print("Attempting to connect to SSID: ");
    Serial.println(SSID);
    // Connect to WPA/WPA2 network. Change this line if using open or WEP network:
    wifiStatus = WiFi.begin(SSID, PASSWORD);
    // wait 10 seconds for connection:
    delay(10000);

    pinMode(BUTTON_PIN, OUTPUT);
    digitalWrite(BUTTON_PIN, HIGH);
    delay(5);
    pinMode(BUTTON_PIN, INPUT);
    bool buttonState = digitalRead(BUTTON_PIN);

    if (buttonState == 0) {
      printOnDisplay("Anulowanie", "Prosze czekac");
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
  balanceCentre = config.minServo + ((config.maxServo - config.minServo) / 2);

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

  printOnDisplay("WiFi Con", "Prosze czekac");
  connectToWiFi();

  printOnDisplay("RTC Update", "Prosze czekac");
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

  printOnDisplay("Test serwa", "Prosze czekac");
  servo.attach(ANALOG_OUTPUT_PIN);  // attaches the servo on pin 9 to the servo object
  topServo.attach(TOP_SERVO);

  pwm.begin();
  pwm.setPWMFreq(50);

  cj125Init();
  start();

  Serial.println("WERSJA 1");

  lastServoBalanceAdj = 0;
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

  RTCTime time;
  RTC.getTime(time);
  auto tm = time.getTmTime();
  str += time.toString();

  str += "\", \"oxygen\": ";
  str += String(oxygen);

  str += ", \"target\": ";
  str += String(config.targetOxygen);

  str += ", \"servo\": ";
  str += String(servo.read());

  str += ", \"pump\": ";
  str += String(pumpStatus);

  str += ", \"balance\": ";
  str += String(servoBalance);

  str += ", \"topServo\": ";
  str += String(topServo.read());

  str += ", \"topServoStatus\": ";
  str += String(topServoStatus);

  str += " }";

  server.broadcastTXT(str);
}

void getData(uint8_t* payload) {
  auto str = String((char*)payload);

  if (str[0] == 'T') {
    float temp = str.substring(2, str.length()).toFloat();
    if (temp < 0.05) return;
    config.targetOxygen = temp;
    Serial.print("New target oxygen: ");
    Serial.println(config.targetOxygen);
    EEPROM.put(0, config);
  } else if (str[0] == 'C') {
    float temp = str.substring(2, str.length()).toFloat();
    if (temp < 0.05) return;
    config.oxygenPumpCutOut = temp;
    Serial.print("New Oxygen pump cut out value: ");
    Serial.println(config.oxygenPumpCutOut);
    EEPROM.put(0, config);
  } else if (str[0] == 'R') {
    pumpStatus = true;
    pinMode(PUMPS_SSR_PIN, OUTPUT);
    digitalWrite(PUMPS_SSR_PIN, HIGH);
  } else if (str[0] == 'S') {
    topServoStatus = true;
    topServo.write(130);
  } else if (str[0] == 'O') {
    float temp = str.substring(2, str.length()).toFloat();
    if (temp < 0.05) return;
    config.oxygenTopServoCutOut = temp;
    Serial.print("New top servo cut out value: ");
    Serial.println(config.oxygenTopServoCutOut);
    EEPROM.put(0, config);
  } else if (str[0] == 'I') {
    int temp = str.substring(2, str.length()).toInt();
    if (temp < 0) return;
    config.minServo = temp;
    Serial.print("New min servo value: ");
    Serial.println(config.minServo);
    EEPROM.put(0, config);
  } else if (str[0] == 'A') {
    int temp = str.substring(2, str.length()).toInt();
    if (temp < 0) return;
    config.maxServo = temp;
    Serial.print("New max servo value: ");
    Serial.println(config.maxServo);
    EEPROM.put(0, config);
  } else if (str[0] == 'U') {
    float temp = str.substring(2, str.length()).toFloat();
    if (temp < 0.05) return;
    config.multiReal = temp;
    Serial.print("New multiplier real value: ");
    Serial.println(config.multiReal);
    EEPROM.put(0, config);
  } else if (str[0] == 'E') {
    float temp = str.substring(2, str.length()).toFloat();
    if (temp < 0.05) return;
    config.boostReal = temp;
    Serial.print("New boost real value: ");
    Serial.println(config.boostReal);
    EEPROM.put(0, config);
  } else if (str[0] == 'M') {
    float temp = str.substring(2, str.length()).toFloat();
    if (temp < 0.05) return;
    config.multiMax = temp;
    Serial.print("New multiplier maximum value: ");
    Serial.println(config.multiMax);
    EEPROM.put(0, config);
  } else if (str[0] == 'V') {
    int temp = str.substring(2, str.length()).toInt();
    if (temp < 0) return;
    config.servoBalanceCooldown = temp;
    Serial.print("New servo balance cooldown value: ");
    Serial.println(config.servoBalanceCooldown);
    EEPROM.put(0, config);
  } else if (str[0] == 'L') {
    int temp = str.substring(2, str.length()).toInt();
    if (temp < 0) return;
    config.balanceMulti = temp;
    Serial.print("New balance multiplier value: ");
    Serial.println(config.balanceMulti);
    EEPROM.put(0, config);
  } else if (str[0] == 'X') {
    int temp = str.substring(2, str.length()).toInt();
    if (temp < 0) return;
    config.maxBalance = temp;
    Serial.print("New max balance value: ");
    Serial.println(config.maxBalance);
    EEPROM.put(0, config);
  }
}

//Infinite loop.
void loop() {
  auto time = millis();

  //detect time overflow
  if (time < wsTime) {
    wsTime = time;
    displayTime = time;
    lastServoBalanceAdj = time;
  }

  cj125Update();

  if (oxygen > config.oxygenPumpCutOut) {
    pumpStatus = false;
    pinMode(PUMPS_SSR_PIN, OUTPUT);
    digitalWrite(PUMPS_SSR_PIN, LOW); 
  }

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

  if (buttonState == 0) {
    if (config.targetOxygen < 25.0) {
      config.targetOxygen += 0.25;
    } else if (config.targetOxygen >= 25) {
      config.targetOxygen = 0;
    }
  }

  float diff = config.targetOxygen - oxygen;
  int temp_angle = servo.read();

  //
  // First step:
  // Adjust servo angle in real time
  //

  if (diff > 0.0 && temp_angle >= config.minServo) {
    temp_angle = temp_angle - ceil(diff) * config.multiReal;
    if (diff >= 3) temp_angle = temp_angle - config.boostReal;
  } else if (diff < 0.0 && temp_angle <= config.maxServo) {
    temp_angle = temp_angle - floor(diff) * config.multiReal;
    if (diff <= -3) temp_angle = temp_angle + config.boostReal;
  }

  float max_angle = abs(config.multiMax * diff);

  if (time >= lastServoBalanceAdj + config.servoBalanceCooldown) {
    lastServoBalanceAdj = time;

    if (diff > 0.2) {
      servoBalance -= config.balanceMulti;
    } else if (diff < -0.2) {
      servoBalance += config.balanceMulti;
    } if (diff < -0.5) {
      servoBalance += config.balanceMulti;
    } else if (diff > 0.5) {
      servoBalance -= config.balanceMulti;
    } if (diff < -1.0) {
      servoBalance += config.balanceMulti;
    } else if (diff > 1.0) {
      servoBalance -= config.balanceMulti;
    }

    if (servoBalance > config.maxBalance) servoBalance = config.maxBalance;
    if (servoBalance < -config.maxBalance) servoBalance = -config.maxBalance;
  }

  if (abs(diff) > 0.25) max_angle *= 0.75 + min(abs(diff), 1.75);

  if (diff > 0.0) {
    temp_angle = max(balanceCentre + servoBalance - max_angle, temp_angle);
  } else { 
    temp_angle = min(balanceCentre + servoBalance + max_angle, temp_angle);
  }

  //if (oxygen < 0.05) temp_angle=40;

  if (temp_angle < config.minServo) temp_angle = config.minServo;
  if (temp_angle > config.maxServo) temp_angle = config.maxServo;

  pwm.setPWM(temp_angle);

  if (DISPLAY_COOLDOWN + displayTime <= time) {
    String status = "Z: ";
    status += String(config.targetOxygen);

    String status2 = "O: ";
    status2 += String(oxygen);

    printOnDisplay(status.c_str(), status2.c_str());

    displayTime = time;
  }
}
