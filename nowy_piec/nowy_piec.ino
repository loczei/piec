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

int wifiStatus = WL_IDLE_STATUS;
WiFiUDP Udp; // A UDP instance to let us send and receive packets over UDP
NTPClient timeClient(Udp);

WebSocketsServer server(80);

bool wifi = false;

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

Config config;

void printOnDisplay(const char* line1, const char* line2) {
  display.clearDisplay();
  display.setCursor(0,0);
  display.println(line1);
  display.println(line2);

  display.setCursor(0,0);
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
  balanceCentre = config.minServoAngle + ((config.maxServoAngle - config.minServoAngle) / 2);

<<<<<<< HEAD
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // Address 0x3C for 128x32
=======
#if BOARD_ONLY
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C); // Address 0x3C for 128x32
>>>>>>> parent of 2426165 (update)
  display.display();
  delay(100);
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.println("LINE 1");
  display.println("LINE 2");

  display.setCursor(0,0);
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

  servo.write(40);// OTWARTA
  topServo.write(130);   
  delay(1000);
  servo.write(130);  // ZAMKNIĘTA
  topServo.write(40);
  delay(1000);
  servo.write(40);
  topServo.write(130);
  delay(1000);

<<<<<<< HEAD
=======
#if BOARD_ONLY
>>>>>>> parent of 2426165 (update)
  cj125Init();
  start();

  Serial.println("WERSJA 1");


  lastServoBalanceAdj = 0;
}

<<<<<<< HEAD
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
=======
int rando() {
  return random(-1, 1);
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED: {
      //Serial.print(num);
      //Serial.println(" WebSocket client disconnect");
>>>>>>> parent of 2426165 (update)
      break;
    }
    case WStype_CONNECTED: {
      Serial.print("WebSocket Connect!");
		  server.sendTXT(num, "Connected");
    } break;
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
  auto str = String((char *)payload);

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

  if (buttonState==0) {
    if (config.targetOxygen < 25.0) {
      config.targetOxygen += 0.25;
    } else if (config.targetOxygen >= 25) {
      config.targetOxygen = 0;
    }
  }

  

  float diff = config.targetOxygen - oxygen;
  int temp_angle = servo.read();

<<<<<<< HEAD
  //
  // First step:
  // Adjust servo angle in real time
  //

  if (diff > 0.0 && temp_angle >= config.minServoAngle) {
    temp_angle = temp_angle - ceil(diff) * config.multiReal;
    if (diff >= 3) temp_angle = temp_angle - config.boostReal;
  } else if (diff < 0.0 && temp_angle <= config.maxServoAngle) {
    temp_angle = temp_angle - floor(diff) * config.multiReal;
    if (diff <= -3) temp_angle = temp_angle + config.boostReal;
  }

  float max_angle = abs(config.multiMax * diff);

  if (time >= lastServoBalanceAdj + config.servoBalanceCooldown) {
    lastServoBalanceAdj = time;

    if (diff > 0.2) {
      servoBalance -= 1;
=======
  if (diff > 0.0 && temp_angle >= 35) {
    temp_angle=temp_angle-ceil(diff) * 2;
    if (diff >= 3) temp_angle=temp_angle-2;
  }

  if (((diff)<0)  && temp_angle<=130 ) {
    temp_angle=temp_angle-floor(diff) * 2;
    if (diff <= -3) temp_angle=temp_angle+2;
  } 
  float max_angle = abs(45 * (diff / 1.5));

  int topServoChange = 0;

  if (time >= lastServoBalanceAdj + SERVO_BALANCE_COOLDOWN) {
    lastServoBalanceAdj = time;
    // Serial.println("3 seconds passed!");

    topServoChange = 1;

    if (diff > 0.2) { 
      servoBalance -= 1;
      topServoChange -=6;
>>>>>>> parent of 2426165 (update)
    } else if (diff < -0.2) {
      servoBalance += 1;
    } if (diff < -0.5) {
      servoBalance += 1;
    } else if (diff > 0.5) {
      servoBalance -= 1;
<<<<<<< HEAD
    } if (diff < -1.0) {
      servoBalance += 1;
    } else if (diff > 1.0) {
      servoBalance -= 1;
    }

    if (servoBalance > 50) servoBalance = 50;
    if (servoBalance < -50) servoBalance = -50;
  }

  if (abs(diff) > 0.25) max_angle *= 0.75 + min(abs(diff), 1.75);

  if (diff > 0.0) {
    temp_angle = max(balanceCentre + servoBalance - max_angle, temp_angle);
  } else { 
    temp_angle = min(balanceCentre + servoBalance + max_angle, temp_angle);
  }

  //if (oxygen < 0.05) temp_angle=40;

  if (temp_angle < config.minServoAngle) temp_angle = config.minServoAngle;
  if (temp_angle > config.maxServoAngle) temp_angle = config.maxServoAngle;

=======
      topServoChange -= 5;
    } if (diff < -1.0) {
      servoBalance += 1;
    } else if (diff > 1.0) { 
      servoBalance -= 1;
      topServoChange -= 10;
    }

    Serial.println(servoBalance);

    // servoBalance += ceil(max(diff, 1.0));
    
    if (servoBalance > 50) servoBalance = 50;
    if (servoBalance < -50) servoBalance = -50;
  }
  
  if (abs(diff) > 0.25) max_angle *= 0.75 + min(abs(diff), 1.75);

  if (diff > 0.0) {
    temp_angle = max(85 + servoBalance - max_angle, temp_angle);
  } else {
    temp_angle = min(85 + servoBalance + max_angle, temp_angle);
  }

  if (oxygen < 0.05) temp_angle=40;

  if (temp_angle <40) temp_angle=40;
  if (temp_angle >130) temp_angle=130;
    
>>>>>>> parent of 2426165 (update)
  servo.write(temp_angle);

  if (oxygen > config.oxygenTopServoCutOut) {
    topServoStatus = false;
    topServo.write(40);
  }

  if (topServoStatus && topServoChange != 0) {
    int topServoAngle = topServo.read();
    if (topServoChange == 1) {
<<<<<<< HEAD
      topServoChange += 3;
    }history-button
=======
      topServoChange += 19;
    }

    topServoAngle = topServoAngle + topServoChange;
    if (topServoAngle < 40) topServoAngle = 40;
    if (topServoAngle > 130) topServoAngle = 130;

    topServo.write(topServoAngle);
  }
  

  if (DISPLAY_COOLDOWN + displayTime <= time) {
>>>>>>> parent of 2426165 (update)
    String status = "Z: ";
    status += String(config.targetOxygen);

    String status2 = "O: ";
    status2 += String(oxygen);

    printOnDisplay(status.c_str(), status2.c_str());

    displayTime = time;
  }
<<<<<<< HEAD

=======
  
>>>>>>> parent of 2426165 (update)
  delay(10);
}
