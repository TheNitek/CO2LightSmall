#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266Ping.h>
#include <WiFiUdp.h>
#include <WiFiManager.h>
#include <ESP8266HTTPClient.h>
#include <UniversalTelegramBot.h>
#include <SoftwareSerial.h>
#include <AirGradient.h>
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
#define WIFI_CHECK_INTERVAL 1*30*1000
#define TELEGRAM_HANDLE_INTERVAL 5*1000

WiFiManager wifiManager;

SoftwareSerial serialCO2(CO2_RX, CO2_TX);
AirGradient ag = AirGradient();
struct {
  int co2 = -1;
  uint8_t errorCount = 0;
} measurement;

Adafruit_NeoPixel pixels(LED_NUMPIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);
uint8_t circleLedCurrent = 0;
uint8_t breatheBrightness = 0;
enum color {RED_BLINK, RED, YELLOW, GREEN, DARK};
color currentColor = DARK;

const char* NTP_SERVER = "de.pool.ntp.org";
const char* TIME_ZONE = "CET-1CEST,M3.5.0,M10.5.0/3";
//bool offline = false;

const char* apiUrl=API_URL;
const char* apiKey=API_KEY;

X509List cert(TELEGRAM_CERTIFICATE_ROOT);
WiFiClientSecure client;
UniversalTelegramBot bot(BOT_TOKEN, client);

Scheduler runner;
void co2UpdateCallback();
void ledBreatheCallback() { breatheLed(); };
void wifiCheckCallback();
void allThingsCallback() { allThingsSend(); };
void telegramCallback();
Task co2Task(CO2_UPDATE_INTERVAL, TASK_FOREVER, &co2UpdateCallback, &runner, true);
Task breatheTask(LED_UPDATE_INTERVAL, TASK_FOREVER, &ledBreatheCallback, &runner, false);
Task wifiTask(WIFI_CHECK_INTERVAL, TASK_FOREVER, &wifiCheckCallback, &runner, true);
Task allThingsTask(ALLTHINGS_UPDATE_INTERVAL, TASK_FOREVER, &allThingsCallback, &runner, true);
Task telegramTask(TELEGRAM_HANDLE_INTERVAL, TASK_FOREVER, &telegramCallback, &runner, true);


void co2UpdateCallback() {
  int co2 = ag.getCO2_Raw();

  Serial.print(" ");
  Serial.println(co2);

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

void circleLed() {
  pixels.clear();
  pixels.setPixelColor(circleLedCurrent, pixels.Color(0, 0, 150));
  pixels.show();

  circleLedCurrent = (circleLedCurrent+1) % LED_NUMPIXELS;
}

void setRingColor(uint32_t color) {
  pixels.fill(color);
  pixels.setBrightness(255);
  pixels.show();
}

void breatheLed() {
  pixels.fill(pixels.Color(150, 0, 0));
  pixels.setBrightness(pixels.sine8(breatheBrightness));
  pixels.show();

  breatheBrightness += 2;
}

boolean isQuietTime() {
  /*time_t now = time(nullptr);
  tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
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
      setRingColor(pixels.Color(255, 255, 255));
      return;
  }

  if(isQuietTime()) {
    if(currentColor != DARK) {
      breatheTask.disable();
      pixels.clear();
      currentColor = DARK;
      Serial.println("dark");
    }
  } else {
    if(measurement.co2 < 600) {
      if(currentColor != GREEN) {
        breatheTask.disable();
        setRingColor(pixels.Color(0, 150, 0));
        currentColor = GREEN;
        Serial.println("green");
      }
    } else if(measurement.co2 < 1000) {
      if(currentColor != YELLOW) {
        breatheTask.disable();
        setRingColor(pixels.Color(150, 150, 0));
        currentColor = YELLOW;
        Serial.println("yellow");
      }
    } else if(measurement.co2 < 1500) {
      if(currentColor != RED) {
        breatheTask.disable();
        setRingColor(pixels.Color(150, 0, 0));
        currentColor = RED;
        Serial.println("red");
      }
    } else {
      if(currentColor != RED_BLINK) {
        breatheTask.enable();
        currentColor = RED_BLINK;
        Serial.println("red blink");
      }
    }
  }
}

void wifiCheckCallback() {
  if (!WiFi.isConnected() || !Ping.ping(WiFi.gatewayIP(), 2)) {

    Serial.println("Reconnecting to WiFi...");
    wifiManager.disconnect();
    connectWifi();

    if(!WiFi.isConnected()) {
      Serial.println("Still not connected.");
      ESP.restart();
    }
  }
}

void connectWifi() {
  Serial.println("Connecting Wifi");

  wifiManager.setDebugOutput(false);
  wifiManager.setConnectRetries(5);
  wifiManager.setShowInfoUpdate(false);
  wifiManager.setCleanConnect(true);
  wifiManager.setTimeout(30);
  wifiManager.autoConnect("co2light", "co2co2co2");

  Serial.print("WiFi connected with IP: "); Serial.println(WiFi.localIP());
}

boolean allThingsSend() {
  if(measurement.co2 < 0 || measurement.co2 >= 5000 || !WiFi.isConnected()) {
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  if(!http.begin(client, apiUrl)) {
    Serial.println("Could not begin HTTPClient");
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
    Serial.println("Sent data");
    return true;
  }

  Serial.print("Error on sending PUT: ");
  Serial.println(httpResponseCode);
  Serial.println(ESP.getFreeHeap());
  return false;
}

void telegramCallback() {
  if(!WiFi.isConnected()) {
    return;
  }

  int numNewMessages = bot.getUpdates(bot.last_message_received + 1);

  while (numNewMessages) {
    Serial.println("got message");
    for (int i = 0; i < numNewMessages; i++) {
      Serial.print("ChatId: "); Serial.println(bot.messages[i].chat_id);
      if (bot.messages[i].text == "/status") {
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
      Serial.println("Failed to send status");
      return false;
    }
};

void setup() {
  Serial.begin(115200);
  Serial.println();

  Serial.println(ESP.getResetReason());

  /*rst_info *rstInfo = ESP.getResetInfoPtr();
  if(rstInfo->reason == REASON_EXCEPTION_RST) {
    Serial.println("Attempting clean restart");
    ESP.reset();
  }*/

  pixels.begin();

  circleLed();
  connectWifi();
  configTzTime(TIME_ZONE, NTP_SERVER);

  circleLed();
  serialCO2.begin(9600);
  ag.CO2_Init(&serialCO2);

  circleLed();
  client.setTrustAnchors(&cert);
  if(WiFi.isConnected()) {
    bot.sendMessage(ADMIN_CHAT_ID, "Reboot: " + ESP.getResetReason(), "");
  }

  co2Task.setSchedulingOption(TASK_SCHEDULE_NC);
  breatheTask.setSchedulingOption(TASK_SCHEDULE_NC);
  wifiTask.setSchedulingOption(TASK_SCHEDULE_NC);
  allThingsTask.setSchedulingOption(TASK_SCHEDULE_NC);
  telegramTask.setSchedulingOption(TASK_SCHEDULE_NC);

  runner.startNow();
}

void loop() {
  runner.execute();
}