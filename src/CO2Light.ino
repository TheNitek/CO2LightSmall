#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>
#include <UniversalTelegramBot.h>
#include <SoftwareSerial.h>
#include "SensorS8.h"
#include <Adafruit_NeoPixel.h>
#define _TASK_SLEEP_ON_IDLE_RUN
#define _TASK_SCHEDULING_OPTIONS
#include <TaskScheduler.h>
#include <time.h>
#include <coredecls.h>    
#include "config.h"

#define CO2_TX D8
#define CO2_RX D7

#define LED_PIN D2
#define LED_NUMPIXELS 12
#define LED_INTERVAL 50

#define CO2_UPDATE_INTERVAL 30*1000
#define LED_UPDATE_INTERVAL 50
#define ALLTHINGS_UPDATE_INTERVAL 1*60*1000
#define WIFI_CHECK_INTERVAL 5*1000
#define TELEGRAM_HANDLE_INTERVAL 5*1000

ESP8266WiFiMulti wifiMulti;
const uint32_t wifiConnectTimeout = 10*1000;

SoftwareSerial debugSerial(-1, TX);
#define DEBUG debugSerial

//SoftwareSerial serialCO2(CO2_RX, CO2_TX);
SensorS8 co2Sensor = SensorS8();
struct {
  int co2 = -1;
  uint8_t errorCount = 0;
} measurement;

Adafruit_NeoPixel pixels(LED_NUMPIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);
uint8_t circleLedCurrent = 0;
uint8_t breatheBrightness = 0;
enum color {RED_BLINK, RED, YELLOW, GREEN, DARK};
color currentColor = DARK;
uint32_t currentColorValue = Adafruit_NeoPixel::Color(0, 0, 150);

const char* NTP_SERVER = "de.pool.ntp.org";
const char* TIME_ZONE = "CET-1CEST,M3.5.0,M10.5.0/3";

bool isFirstConnect = true;
uint8_t httpErrorCount = 0;

const char* apiUrl=API_URL;
const char* apiKey=API_KEY;

X509List cert(TELEGRAM_CERTIFICATE_ROOT);
WiFiClientSecure client;
UniversalTelegramBot bot(BOT_TOKEN, client);

Scheduler runner;
void co2UpdateCallback();
void ledCircleCallback(){ circleLed(); };
void ledBreatheCallback();
void wifiCheckCallback();
void allThingsCallback() { allThingsSend(); };
void telegramCallback();
Task co2Task(CO2_UPDATE_INTERVAL, TASK_FOREVER, &co2UpdateCallback, &runner, true);
Task circleTask(LED_UPDATE_INTERVAL, TASK_FOREVER, &ledCircleCallback, &runner, true);
Task breatheTask(LED_UPDATE_INTERVAL, TASK_FOREVER, &ledBreatheCallback, &runner, false);
Task wifiTask(WIFI_CHECK_INTERVAL, TASK_FOREVER, &wifiCheckCallback, &runner, true);
Task allThingsTask(ALLTHINGS_UPDATE_INTERVAL, TASK_FOREVER, &allThingsCallback, &runner, true);
Task telegramTask(TELEGRAM_HANDLE_INTERVAL, TASK_FOREVER, &telegramCallback, &runner, true);


void co2UpdateCallback() {
  int co2 = co2Sensor.getCO2();

  DEBUG.print(" ");
  DEBUG.println(co2);

  if(co2 < 0) {
    measurement.errorCount++;
    if(measurement.errorCount == 3) {
        measurement.co2 = -1;
    }
    return;
  }
  measurement.errorCount = 0;
  measurement.co2 = co2;

  setLedColor();
}
void ledBreatheCallback() {
  if(!circleTask.isEnabled()) {
    breatheLed();
  }
};


void circleLed() {
  pixels.clear();
  pixels.setPixelColor(circleLedCurrent, currentColorValue);
  if(breatheTask.isEnabled()) {
    pixels.setBrightness(Adafruit_NeoPixel::sine8(breatheBrightness));
    breatheBrightness += 2;
  }
  pixels.show();

  circleLedCurrent = (circleLedCurrent+1) % LED_NUMPIXELS;
}

void setRingColor(uint32_t color) {
  pixels.fill(color);
  pixels.setBrightness(255);
  pixels.show();
}

void breatheLed() {
  pixels.fill(currentColorValue);
  pixels.setBrightness(Adafruit_NeoPixel::sine8(breatheBrightness));
  pixels.show();

  breatheBrightness += 2;
}

boolean isQuietTime() {
  /*time_t now = time(nullptr);
  tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    DEBUG.println("Failed to obtain time");
    return false;
  }
  if(timeinfo.tm_hour >= 20) {
    return true;
  }
  switch(timeinfo.tm_wday) {
    case 0:
    case 6:
      // Weekend
      return true;
    default:
      // Not weekend
      return (timeinfo.tm_hour < 6);
  }*/
  return false;
}

void setLedColor() {
  if(measurement.co2 < 0) {
      currentColorValue = Adafruit_NeoPixel::Color(255, 255, 255);
      setRingColor(currentColorValue);
      return;
  }

  if(isQuietTime()) {
    if(currentColor != DARK) {
      breatheTask.disable();
      pixels.clear();
      currentColor = DARK;
      DEBUG.println("dark");
    }
  } else {
    if(measurement.co2 < 600) {
      if(currentColor != GREEN) {
        breatheTask.disable();
        currentColorValue = Adafruit_NeoPixel::Color(0, 150, 0);
        setRingColor(currentColorValue);
        currentColor = GREEN;
        DEBUG.println("green");
      }
    } else if(measurement.co2 < 1000) {
      if(currentColor != YELLOW) {
        breatheTask.disable();
        currentColorValue = Adafruit_NeoPixel::Color(150, 150, 0);
        setRingColor(currentColorValue);
        currentColor = YELLOW;
        DEBUG.println("yellow");
      }
    } else if(measurement.co2 < 1500) {
      if(currentColor != RED) {
        breatheTask.disable();
        currentColorValue = Adafruit_NeoPixel::Color(150, 0, 0);
        setRingColor(currentColorValue);
        currentColor = RED;
        DEBUG.println("red");
      }
    } else {
      if(currentColor != RED_BLINK) {
        currentColorValue = Adafruit_NeoPixel::Color(150, 0, 0);
        currentColor = RED_BLINK;
        breatheTask.enableIfNot();
        DEBUG.println("red blink");
      }
    }
  }
}

void connectWifi() {
  DEBUG.println("Connecting Wifi");

  if(wifiMulti.run(wifiConnectTimeout) != WL_CONNECTED) {
    circleTask.enableIfNot();
    return;
  }

  DEBUG.print("WiFi connected: ");
  DEBUG.print(WiFi.SSID());
  DEBUG.print(" ");
  DEBUG.println(WiFi.localIP());
  httpErrorCount = 0;
  circleTask.disable();

  if(isFirstConnect) {
    bot.sendMessage(ADMIN_CHAT_ID, "Reboot: " + ESP.getResetReason(), "");
    isFirstConnect=false;
  }
}

void wifiCheckCallback() {
  if(WiFi.isConnected() && (httpErrorCount > 2)) {
    DEBUG.println("Reconnecting to WiFi...");
    WiFi.disconnect();
  }

  if(!WiFi.isConnected()) {
    connectWifi();
  }
}

boolean allThingsSend() {
  if(measurement.co2 <= 0 || measurement.co2 >= 5000 || !WiFi.isConnected()) {
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  if(!http.begin(client, apiUrl)) {
    DEBUG.println("Could not begin HTTPClient");
    return false;
  }
  http.addHeader("Authorization", apiKey);
  http.addHeader("Content-Type", "application/json");

  const char* payloadTpl = "{\"co2\": {\"value\": %d}, \"uptime\": {\"value\": %lu}}";
  char payload[200] = "";
  unsigned long uptime = millis() / 1000;
  snprintf(payload, sizeof(payload), payloadTpl, measurement.co2, uptime);
  int httpResponseCode = http.PUT(payload);

  if(httpResponseCode==HTTP_CODE_NO_CONTENT) {
    DEBUG.println("Sent data");
    return true;
  }

  DEBUG.print("Error on sending PUT: ");
  DEBUG.println(httpResponseCode);
  DEBUG.println(ESP.getFreeHeap());
  if(httpResponseCode < 0) {
    httpErrorCount++;
  }
  return false;
}

void telegramCallback() {
  if(!WiFi.isConnected()) {
    return;
  }

  int numNewMessages = bot.getUpdates(bot.last_message_received + 1);

  while (numNewMessages) {
    DEBUG.println("got message");
    for (int i = 0; i < numNewMessages; i++) {
      DEBUG.print("ChatId: "); DEBUG.println(bot.messages[i].chat_id);
      if (bot.messages[i].text.startsWith("/status")) {
        sendStatusTelegram(bot.messages[i].chat_id);
      }
    }
    numNewMessages = bot.getUpdates(bot.last_message_received + 1);
  }
}

boolean sendStatusTelegram(String recipient) {
    char msgBuffer[1000];
    
    snprintf(msgBuffer, sizeof(msgBuffer), "CO2: %uppm\nHeap: %u\n", measurement.co2, ESP.getFreeHeap());
    if(bot.sendMessage(recipient, String(msgBuffer), "")) {
      return true;
    } else {
      DEBUG.println("Failed to send status");
      return false;
    }
};

void setup() {
  Serial.begin(9600);
  Serial.swap();

  DEBUG.begin(9600);
  DEBUG.println();

  DEBUG.println(ESP.getResetReason());

  pixels.begin();

  circleLed();
  co2Sensor.debug = true;
  co2Sensor.begin(&Serial, &DEBUG);
  int abc = co2Sensor.getABCPeriod();
  if(abc >= 0) {
    DEBUG.printf("ABC period: %.2f\n", abc/24.0f);
  } else {
    DEBUG.println("Could not read ABC period");
  }

  circleLed();
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  wifiMulti.addAP(WIFI1_SSID, WIFI1_PASS);
  wifiMulti.addAP(WIFI2_SSID, WIFI2_PASS);
  connectWifi();
  configTzTime(TIME_ZONE, NTP_SERVER);

  circleLed();
  client.setTrustAnchors(&cert);

  co2Task.setSchedulingOption(TASK_SCHEDULE_NC);
  circleTask.setSchedulingOption(TASK_SCHEDULE_NC);
  circleTask.setOnDisable([]() {
    setRingColor(currentColorValue);
  });
  breatheTask.setSchedulingOption(TASK_SCHEDULE_NC);
  wifiTask.setSchedulingOption(TASK_SCHEDULE_NC);
  allThingsTask.setSchedulingOption(TASK_SCHEDULE_NC);
  telegramTask.setSchedulingOption(TASK_SCHEDULE_NC);

  runner.startNow();
}

void loop() {
  runner.execute();
}