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

#define BOARD_ONLY 0

Adafruit_SSD1306 display = Adafruit_SSD1306(128, 32, &Wire);
Servo servo;
int displayStep = 0;

float oxygen = 0.0;
float lambda = 0.0;
bool pumpStatus = true;
int servo_bias = 0;

char ssid[] = "";
char pass[] = "";
int wifiStatus = WL_IDLE_STATUS;
WiFiUDP Udp; // A UDP instance to let us send and receive packets over UDP
NTPClient timeClient(Udp);

WebSocketsServer server(80);

bool wifi = false;

struct Config {
  float targetOxygen;
  float oxygenPumpCutOut;
};

Config config;

void printOnDisplay(const char* line1, const char* line2) {
#if BOARD_ONLY
  display.clearDisplay();
  display.setCursor(0,0);
  display.println(line1);
  display.println(line2);

  display.setCursor(0,0);
  display.display();
#else
  Serial.println("---------------- Screen ----------------");
  Serial.println(line1);
  Serial.println(line2);
  Serial.println("---------------- sCREEN ----------------");
#endif
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
    Serial.println(ssid);
    // Connect to WPA/WPA2 network. Change this line if using open or WEP network:
    wifiStatus = WiFi.begin(ssid, pass);
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

#if BOARD_ONLY
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C); // Address 0x3C for 128x32
  display.display();
  delay(100);
#endif

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
#if BOARD_ONLY
  servo.attach(ANALOG_OUTPUT_PIN);  // attaches the servo on pin 9 to the servo object


  servo.write(40);   // OTWARTA
  delay(1000);
  servo.write(130);  // ZAMKNIĘTA
  delay(1000);
  servo.write(40);
  delay(1000);
#endif

#if BOARD_ONLY
  cj125Init();

  //Start main function.
  start();
#endif
}

int rando() {
  return random(-1, 1);
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED: {
      //Serial.print(num);
      //Serial.println(" WebSocket client disconnect");
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
#if BOARD_ONLY
  str += String(oxygen);
#else
  str += String(random(15, 20));
#endif

  str += ", \"target\": ";
  str += String(config.targetOxygen);

  str += ", \"servo\": ";
  str += String(servo.read());

  str += ", \"pump\": ";
  str += String(pumpStatus);

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
  }
}

//Infinite loop.
void loop() {
#if BOARD_ONLY
  cj125Update();
#else
  oxygen += rando();
#endif

  if (oxygen > config.oxygenPumpCutOut) {
    pumpStatus = false;
    digitalWrite(BUTTON_PIN, LOW);
  } else {
    pumpStatus = true;
    digitalWrite(BUTTON_PIN, HIGH);
  }

  if (wifi) {
    server.loop();
    sendData();
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

  String status = "Z: ";
  status += String(config.targetOxygen);
  status += " O: ";
  status += String(oxygen);

  float diff = config.targetOxygen - oxygen;
  int temp_angle = servo.read();

  if (diff > 0.0 && temp_angle >= 35) {
    temp_angle=temp_angle-ceil(diff) * 2;
  if ((diff)>=3)temp_angle=temp_angle-2;
  }

  if (((diff)<0)  && temp_angle<=130 )
  {
  temp_angle=temp_angle-floor(diff) * 2;
  if ((diff)<=-3)temp_angle=temp_angle+2;
  } 
  float max_angle = abs(45 * (diff / 1.5));

  /* if (ODCHYLENIE_STEP >= 20) {
    if (TLEN_ROZNICA > 0.0) {
      ODCHYLENIE += ceil(max(TLEN_ROZNICA, 1.0));
    } else if (TLEN_ROZNICA < 0.0) {
      ODCHYLENIE += floor(min(TLEN_ROZNICA, -1.0));
    } 
    ODCHYLENIE += ceil(max(TLEN_ROZNICA, 1.0));
    if (ODCHYLENIE > 50) ODCHYLENIE = 50;
    if (ODCHYLENIE < -50) ODCHYLENIE = -50;

    ODCHYLENIE_STEP = 0;
  } else {
    ODCHYLENIE_STEP++;
  } */

  if (diff > 0.0) {
    temp_angle = max(85 - max_angle, temp_angle);
  } else {
    temp_angle = min(85 + max_angle, temp_angle);
  }

  if (temp_angle <40) temp_angle=40;
  if (temp_angle >130) temp_angle=130;
     
  servo.write(temp_angle);

  printOnDisplay(status.c_str(), WiFi.localIP().toString().c_str());

  delay(10);
}