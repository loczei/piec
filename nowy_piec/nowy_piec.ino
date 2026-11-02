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
bool sendConfig = false;
bool atRest = true;

class Timer {
public:
  unsigned long lastCheck = 0;
  int waitTime = 0;

  void set_wait_time(int newWait) {
    this->waitTime = newWait;
    this->lastCheck = millis();
  }

  bool check_time(unsigned long time = millis()) {
    if (this->waitTime >= time - this->lastCheck) {
      this->lastCheck = time;
      return true;
    }

    return false;
  }
};

Timer normalTimer;
Timer dataTimer;

int wifiStatus = WL_IDLE_STATUS;
WiFiUDP Udp;  // A UDP instance to let us send and receive packets over UDP
NTPClient timeClient(Udp);

WebSocketsServer server(80);

Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver(0x7F, Wire);
int servoPWM = 300;

bool wifi = false;

struct Config {
  // --- Parametry procesu i tlenu ---
  float targetOxygen = 20.9f;
  float oxygenPumpCutOut = 25.0f;

  // --- Pozycje i limity serwa ---
  int maxServo = 415;
  int minServo = 345;
  int startPosition = 400;

  // --- Timery i interwały czasowe ---
  int waitTime = 1000;
  int initialWaitTime = 5000;
  int dataAcquire = 100;

  // --- Strefa martwa i progi stanu spoczynku (atRest) ---
  float deadzone = 0.5f;
  float derRestThreshold = 0.2f;
  float restAlphaThreshold = 0.1f;
  float restRThreshold = 0.8f;
  float restIfAlpha = 0.05f;
  float restIfR = 0.7f;
  float restCorrectionThreshold = 0.15f;

  // --- Parametry ruchu przy wzroście błędu (alpha > 0) ---
  int raiseStep = 10;
  int aggresiveRaise = 0;
  float alphaAgressiveRaising = 0.5f;
  float agressiveRaising = 2.0f;
  int agressiveRaisingAdditional = 5;

  // --- Parametry ruchu przy spadku błędu (alpha <= 0) ---
  int fallStep = 10;
  int aggresiveFall = 0;
  float alphaAgressiveFall = -0.5f;
  float agressiveFall = 2.0f;
  int agressiveFallAdditional = 5;
};

Config defaultConfig;

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

class Data {
public:
  struct LinearRegressionResult {
    float alpha = 0.0f; // nachylenie (a)
    float beta = 0.0f;  // wyraz wolny (b)
    float r2 = 0.0f;    // współczynnik determinacji R^2
  };

  struct QuadraticRegressionResult {
    float a = 0.0f;     // współczynnik przy x^2
    float b = 0.0f;     // współczynnik przy x
    float c = 0.0f;     // wyraz wolny
    float r2 = 0.0f;    // współczynnik determinacji R^2
  };

  static constexpr uint8_t BUFFER_SIZE = 50;
  float data_x[BUFFER_SIZE] = {0};
  float data_y[BUFFER_SIZE] = {0};

  uint16_t lastSend = 0;
  uint8_t count = 0;
  uint8_t head = 0;

  LinearRegressionResult lastLinearResult {};
  QuadraticRegressionResult lastQuadraticResult {};

  struct Avg {
    float x = 0.0f;
    float y = 0.0f;
  };

  Avg calculateAverage() const {
    if (count == 0) return {0.0f, 0.0f};
    float sum_x = 0.0f;
    float sum_y = 0.0f;
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

    if (lastSend < BUFFER_SIZE) lastSend++;
  }

  bool isReady() const {
    return count >= BUFFER_SIZE;
  }

  // --- REGRESJA LINIOWA (y = alpha * x + beta) ---
  LinearRegressionResult calculateLinearRegression() {
    if (!isReady()) {
      return lastLinearResult;
    }

    Avg avg = calculateAverage();

    float cov = 0.0f;
    float var_x = 0.0f;
    float ss_tot = 0.0f;

    for (uint8_t i = 0; i < BUFFER_SIZE; i++) {
      float dx = data_x[i] - avg.x;
      float dy = data_y[i] - avg.y;
      cov += dx * dy;
      var_x += dx * dx;
      ss_tot += dy * dy;
    }

    if (var_x > 0.00001f) {
      lastLinearResult.alpha = cov / var_x;
      lastLinearResult.beta = avg.y - (lastLinearResult.alpha * avg.x);

      // Obliczenie sumy kwadratów reszt (SS_res)
      float ss_res = 0.0f;
      for (uint8_t i = 0; i < BUFFER_SIZE; i++) {
        float y_pred = lastLinearResult.alpha * data_x[i] + lastLinearResult.beta;
        float diff = data_y[i] - y_pred;
        ss_res += diff * diff;
      }

      if (ss_tot > 0.00001f) {
        float r2 = 1.0f - (ss_res / ss_tot);
        lastLinearResult.r2 = (r2 < 0.0f) ? 0.0f : ((r2 > 1.0f) ? 1.0f : r2);
      } else {
        lastLinearResult.r2 = 1.0f; // Brak wariancji w danych y
      }
    }

    return lastLinearResult;
  }

  // --- REGRESJA KWADRATOWA (y = a*x^2 + b*x + c) ---
  QuadraticRegressionResult calculateQuadraticRegression() {
    if (!isReady()) {
      return lastQuadraticResult;
    }

    Avg avg = calculateAverage();

    // Centrujemy x wokół średniej (u = x - avg.x) aby uniknąć ucięć precyzji float przy dużych potęgach
    float s_u1 = 0.0f, s_u2 = 0.0f, s_u3 = 0.0f, s_u4 = 0.0f;
    float s_y = 0.0f, s_uy = 0.0f, s_u2y = 0.0f;
    float ss_tot = 0.0f;

    for (uint8_t i = 0; i < BUFFER_SIZE; i++) {
      float u = data_x[i] - avg.x;
      float y = data_y[i];
      float u2 = u * u;

      s_u1 += u;
      s_u2 += u2;
      s_u3 += u2 * u;
      s_u4 += u2 * u2;

      s_y += y;
      s_uy += u * y;
      s_u2y += u2 * y;

      float dy = y - avg.y;
      ss_tot += dy * dy;
    }

    // Układ równań: M * [a, b_u, c_u]^T = Y
    // Gdzie u = x - avg.x
    float M[3][4] = {
      { s_u4, s_u3, s_u2, s_u2y },
      { s_u3, s_u2, s_u1, s_uy  },
      { s_u2, s_u1, (float)BUFFER_SIZE, s_y }
    };

    // Eliminacja Gaussa z wyborem elementu głównego
    for (int i = 0; i < 3; i++) {
      int maxRow = i;
      for (int k = i + 1; k < 3; k++) {
        if (fabsf(M[k][i]) > fabsf(M[maxRow][i])) {
          maxRow = k;
        }
      }

      for (int k = 0; k <= 3; k++) {
        float tmp = M[maxRow][k];
        M[maxRow][k] = M[i][k];
        M[i][k] = tmp;
      }

      if (fabsf(M[i][i]) < 0.000001f) {
        return lastQuadraticResult; // Macierz osobliwa
      }

      for (int k = i + 1; k < 3; k++) {
        float factor = M[k][i] / M[i][i];
        for (int j = i; j <= 3; j++) {
          M[k][j] -= factor * M[i][j];
        }
      }
    }

    // Podstawianie wsteczne
    float sol[3];
    for (int i = 2; i >= 0; i--) {
      sol[i] = M[i][3];
      for (int j = i + 1; j < 3; j++) {
        sol[i] -= M[i][j] * sol[j];
      }
      sol[i] /= M[i][i];
    }

    float a_fit = sol[0];
    float b_u   = sol[1];
    float c_u   = sol[2];

    // Powrót ze współrzędnych scentrowanych: u = x - x_bar
    // y = a*(x - x_bar)^2 + b_u*(x - x_bar) + c_u
    // a = a_fit
    // b = b_u - 2 * a_fit * x_bar
    // c = c_u - b_u * x_bar + a_fit * x_bar^2
    lastQuadraticResult.a = a_fit;
    lastQuadraticResult.b = b_u - 2.0f * a_fit * avg.x;
    lastQuadraticResult.c = c_u - b_u * avg.x + a_fit * avg.x * avg.x;

    // Obliczenie sumy kwadratów reszt (SS_res) dla regresji kwadratowej
    float ss_res = 0.0f;
    for (uint8_t i = 0; i < BUFFER_SIZE; i++) {
      float u = data_x[i] - avg.x;
      float y_pred = a_fit * u * u + b_u * u + c_u;
      float diff = data_y[i] - y_pred;
      ss_res += diff * diff;
    }

    if (ss_tot > 0.00001f) {
      float r2 = 1.0f - (ss_res / ss_tot);
      lastQuadraticResult.r2 = (r2 < 0.0f) ? 0.0f : ((r2 > 1.0f) ? 1.0f : r2);
    } else {
      lastQuadraticResult.r2 = 1.0f;
    }

    return lastQuadraticResult;
  }

  float get_derivative() const {
    if (count < 2) {
      return 0.0f;
    }

    uint8_t last_idx = (head == 0) ? (BUFFER_SIZE - 1) : (head - 1);
    uint8_t prev_idx = (last_idx == 0) ? (BUFFER_SIZE - 1) : (last_idx - 1);

    float dt = data_x[last_idx] - data_x[prev_idx];
    if (fabsf(dt) < 0.00001f) {
      return 0.0f;
    }

    return (data_y[last_idx] - data_y[prev_idx]) / dt;
  }

  String get_json() {
    String str = "";

    str += ", \"linA\": ";
    str += String(this->lastLinearResult.alpha);
    str += ", \"linB\": ";
    str += String(this->lastLinearResult.beta);
    str += ", \"linR\": ";
    str += String(this->lastLinearResult.r2);

    str += ", \"sqrA\": ";
    str += String(this->lastQuadraticResult.alpha);
    str += ", \"sqrB\": ";
    str += String(this->lastQuadraticResult.beta);
    str += ", \"sqrC\": ";
    str += String(this->lastQuadraticResult.beta);
    str += ", \"sqrR\": ";
    str += String(this->lastQuadraticResult.r2);

    str += ", \"der\": ";
    str += String(this->get_derivative());

    return str;
  }
};

Data data;

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
DataDescription<float> descOP("OP", "Oxygen Pump Cut Out", &configHolder.config.oxygenPumpCutOut, 0.0f, 25.0f);

//integers
DataDescription<int> descMS("MS", "Max Servo", &configHolder.config.maxServo, 0, 4096);
DataDescription<int> descMi("Mi", "Min Servo", &configHolder.config.minServo, 0, 4096);
DataDescription<int> descWT("WT", "Wait Time", &configHolder.config.waitTime, 0, INT_MAX);
DataDescription<int> descSP("SP", "Start Position", &configHolder.config.startPosition, 0, 4096);
DataDescription<int> descIW("IW", "Initial Wait Time", &configHolder.config.initialWaitTime, 0, INT_MAX);
DataDescription<int> descDA("DA", "Data Acquire", &configHolder.config.dataAcquire, 0, INT_MAX);

// Strefa martwa i progi wybudzenia / wejścia w spoczynek
DataDescription<float> descDRT("DRT", "Der Rest Thresh", &configHolder.config.derRestThreshold, 0.0f, 5.0f);
DataDescription<float> descRAT("RAT", "Rest Alpha Thresh", &configHolder.config.restAlphaThreshold, 0.0f, 5.0f);
DataDescription<float> descRRT("RRT", "Rest R Thresh", &configHolder.config.restRThreshold, 0.0f, 1.0f);
DataDescription<float> descRIA("RIA", "Rest If Alpha", &configHolder.config.restIfAlpha, 0.0f, 2.0f);
DataDescription<float> descRIR("RIR", "Rest If R", &configHolder.config.restIfR, 0.0f, 1.0f);
DataDescription<float> descRCT("RCT", "Rest Corr Thresh", &configHolder.config.restCorrectionThreshold, 0.0f, 2.0f);

// Parametry ruchu przy wzroście błędu (alpha > 0)
DataDescription<int>   descRS("RS", "Raise Step", &configHolder.config.raiseStep, 0, 500);
DataDescription<int>  descAR("AR", "Aggresive Raise", &configHolder.config.aggresiveRaise, 0, 1);
DataDescription<float> descAAR("AAR", "Alpha Aggr Raise", &configHolder.config.alphaAgressiveRaising, 0.0f, 10.0f);
DataDescription<float> descAGR("AGR", "Aggr Raising Base", &configHolder.config.agressiveRaising, 1.01f, 10.0f);
DataDescription<int>   descARA("ARA", "Aggr Raise Add", &configHolder.config.agressiveRaisingAdditional, 0, 500);

// Parametry ruchu przy spadku błędu (alpha <= 0)
DataDescription<int>   descFS("FS", "Fall Step", &configHolder.config.fallStep, 0, 500);
DataDescription<int>  descAF("AF", "Aggresive Fall", &configHolder.config.aggresiveFall, 0, 1);
DataDescription<float> descAAF("AAF", "Alpha Aggr Fall", &configHolder.config.alphaAgressiveFall, -10.0f, 0.0f);
DataDescription<float> descAGF("AGF", "Aggr Fall Base", &configHolder.config.agressiveFall, 1.01f, 10.0f);
DataDescription<int>   descAFA("AFA", "Aggr Fall Add", &configHolder.config.agressiveFallAdditional, 0, 500);

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

  if (sendConfig) {
    for (auto d : desc) str += d->get_json();
    sendConfig = false;
  }

  str += data.get_json();

  str += " }";

  server.broadcastTXT(str);
}


void getData(uint8_t* payload) {
  auto str = String((char*)payload);

  sendConfig = true;

  for (auto d : desc) d->update(str);

  normalTimer.set_wait_time(configHolder.config.waitTime);
  overrideTimer.set_wait_time(configHolder.config.checkTime);
  dataTimer.set_wait_time(configHolder.config.dataAcquire);
}

//Infinite loop.
void loop() {
  auto time = millis();
  auto& config = configHolder.config;

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

  if (dataTimer.check_time()) {
    data.updateData(abs(diff));
    data.calculateLinearRegression();
    data.calculateQuadraticRegression();
  }

  auto linReg = data.lastLinearResult;
  auto sqrReg = data.lastQuadraticResult;

  if (time - wsTime >= WEBSOCKET_COOLDOWN) {
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

  if (oxygen > configHolder.config.oxygenPumpCutOut && pump) {
    pump = false;
    pinMode(PUMPS_SSR_PIN, OUTPUT);
    digitalWrite(PUMPS_SSR_PIN, LOW); 
  }

  int servo_move = 0;

  if (normalTimer.check_time()) {
    if ((abs(diff) > config.deadzone ||
        abs(data.get_derivative()) > config.derRestThreshold ||
        (linReg.alpha > config.restAlphaThreshold && linReg.r2 > config.restRThreshold)
      ) && atRest) {
        atRest = false;
    }

    if (!atRest) {
      if (
        abs(diff) < config.deadzone &&
        linReg.alpha < config.restIfAlpha &&
        linReg.r2 > config.restIfR
      ) {
        atRest = true;
      } else if (linReg.alpha > 0) {
        servo_move += config.raiseStep;

        if (
          config.aggresiveRaise && (
            linReg.alpha > config.alphaAgressiveRaising ||
            sqrReg.r2 > linReg.r2 ||
            sqrReg.a > 0
          )
        ) {
          if (config.agressiveRaising > 1) {
            servo_move += ceil(log(abs(diff) + 1.0f) / log(config.agressiveRaising));
          }

          servo_move += config.agressiveRaisingAdditional;
        }
      } else {
        servo_move -= config.fallStep;

        if (
          config.aggresiveFall && (
            linReg.alpha < config.alphaAgressiveFall ||
            abs(diff) > config.deadzone
          )
        ) {
          if (config.agressiveFall > 1) {
            servo_move -= ceil(log(abs(diff) + 1.0f) / log(config.agressiveFall));
          }

          servo_move -= config.agressiveFallAdditional;
        }
      }
    } else if (linReg.alpha > config.restCorrectionThreshold) {
      servo_move += config.raiseStep;
    }
  }

  if (servo_move != 0) {
    int dir = diff > 0 ? 1 : -1;
    int temp_angle = constrain(servoPWM + (dir * servo_move), configHolder.config.minServo, configHolder.config.maxServo);

    servoPWM = temp_angle;
    pwm.setPWM(0, 0, temp_angle);
  }
}
