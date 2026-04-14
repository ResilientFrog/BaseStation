#include "wi-fi/wiFiConnectionController.h"
#include "baseRTK/baseRTKController.h"
#include "logger/Logger.h"
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <LittleFS.h>

#define LOG_FILE "/sensor_log.txt"
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_MOSI 23
#define OLED_CLK  18
#define OLED_DC   16
#define OLED_CS   5
#define OLED_RST  17

Adafruit_SSD1306 display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &SPI,
  OLED_DC,
  OLED_RST,
  OLED_CS
);


enum ScreenMode {
  SCREEN_MAIN = 0,
  SCREEN_RTCM = 1
};


struct RTCMStats {
  uint32_t rtcm1005Count = 0;
  uint32_t lastUpdate = 0;
  double last1005X = 0.0;
  double last1005Y = 0.0;
  double last1005Z = 0.0;
  double last1005LON = 0.0;
  double last1005LAT = 0.0;
  double last1005ALT = 0.0;
  bool has1005Coordinate = false;
};

struct GNSSDisplayCache {
  float accuracyMeters = 0.0f;
  uint8_t fix = 0;
  uint8_t sats = 0;
  bool surveyValid = false;
  bool surveyActive = false;
  unsigned long surveyObservationTime = 0;
  bool initialized = false;
};

RTCMStats rtcmStats;
GNSSDisplayCache gnssDisplayCache;
ScreenMode currentScreen = SCREEN_MAIN;
unsigned long lastScreenSwitch = 0;
const uint16_t SCREEN_SWITCH_INTERVAL = 2000;
const uint16_t DISPLAY_UPDATE_INTERVAL = 1000;
const uint32_t SURVEY_STATUS_LOG_INTERVAL = 30000;
const uint16_t GNSS_POLL_INTERVAL = 100;
const uint16_t DISPLAY_CACHE_REFRESH_INTERVAL = 500;
const uint32_t SPI_FREQUENCY = 1000000;
const uint16_t DELAY_TIME = 2000;
const uint32_t BAUD_RATE = 115200;

void logToFile(String message) {
  File file = LittleFS.open(LOG_FILE, FILE_APPEND);
  if (!file) {
    return;
  }
  file.println(String(millis()) + " - " + message);
  file.close();
}

// Function for Display RTCM 1005 message
void displayRTCM1005() {
  
  RTCM_1005_data_t data;
  uint8_t result = getRTCM1005(&data);
  
  if (result == 2) { 
    rtcmStats.rtcm1005Count++;
    rtcmStats.lastUpdate = millis();
    
    double x = data.AntennaReferencePointECEFX / 10000.0;
    double y = data.AntennaReferencePointECEFY / 10000.0;
    double z = data.AntennaReferencePointECEFZ / 10000.0;

    rtcmStats.last1005X = x;
    rtcmStats.last1005Y = y;
    rtcmStats.last1005Z = z;
    rtcmStats.last1005LON = longitudeData() / 1e7;
    rtcmStats.last1005LAT = latitudeData() / 1e7;
    rtcmStats.last1005ALT = altitudeData() / 1000.0;
    rtcmStats.has1005Coordinate = true;

  }
}


// Display all available RTCM messages on screen
void displayRTCMScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  display.setCursor(0, 0);
  display.println("RTCM Status:");
  display.setCursor(0, 12);
  if (rtcmStats.has1005Coordinate) {
    display.printf("Lon: %.8f", rtcmStats.last1005LON);
    display.setCursor(0, 24);
    display.printf("Lat: %.8f", rtcmStats.last1005LAT);
    display.setCursor(0, 36);
    display.printf("Alt: %.2f", rtcmStats.last1005ALT);
  } else {
    display.println("Waiting for RTCM 1005");
  }
  
  display.display();
}

// Display main status screen
void displayMainScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  float accMeters = gnssDisplayCache.accuracyMeters;
  display.setCursor(0, 0);
  display.printf("Acc: %.2f m", accMeters);
  display.setCursor(0, 15);
  display.printf("Fix: %d  Sats: %d", gnssDisplayCache.fix, gnssDisplayCache.sats);
  display.setCursor(0, 30);
  bool surveyValid = gnssDisplayCache.surveyValid;
  bool surveyActive = gnssDisplayCache.surveyActive;
  bool surveyRequested = (getCurrentBaseMode() == MODE_SURVEY_IN);
  

  if (surveyValid) {
    display.print("Survey: DONE");
    if (rtcmStats.has1005Coordinate) {
      display.setCursor(0, 39);
      display.printf("X: %.4f", rtcmStats.last1005X);
      display.setCursor(0, 47);
      display.printf("Y: %.4f", rtcmStats.last1005Y);
      display.setCursor(0, 55);
      display.printf("Z: %.4f", rtcmStats.last1005Z);
    } 
  } else if (surveyActive || surveyRequested) {
    display.printf("Survey: ACTIVE (%lus)", gnssDisplayCache.surveyObservationTime);
  } else {
    display.print("Survey: INACTIVE");
  }

  display.display();
}

void refreshGNSSDisplayCache() {
  gnssDisplayCache.accuracyMeters = observationAccuracy();
  gnssDisplayCache.fix = fixType();
  gnssDisplayCache.sats = satellites();
  gnssDisplayCache.surveyValid = surveyValidity();
  gnssDisplayCache.surveyActive = surveyActivity();
  gnssDisplayCache.surveyObservationTime = observationTime();
  gnssDisplayCache.initialized = true;
}


void setup() {

  delay(DELAY_TIME);

  Serial.begin(BAUD_RATE);
  delay(50);
  
  SPI.begin(OLED_CLK, -1, OLED_MOSI, OLED_CS);
  SPI.setFrequency(SPI_FREQUENCY);

  if (!display.begin(SSD1306_SWITCHCAPVCC)) {
    logger.logError("Display", "SSD1306 initialization failed");
    while (true);
  }

  display.clearDisplay();
  display.setTextSize(0.5);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("START BASE STATION");
  display.display();

  delay(1500);
 
  if (!LittleFS.begin(true)) {
    logger.logError("System", "LittleFS mount failed");
  }

  initWiFiServer();
  initRTKController();
  lastScreenSwitch = millis();
  BaseConfig values = getBaseConfiguration();

  if (values.mode != MODE_INVALID) {
    bool applied = setMode(values);
    String initMsg = applied ? "Initial settings applied successfully" : "Failed to apply initial settings";
    logger.logInfo("System", initMsg);
   
  } 
}

// Function for main logic - display status on OLED, handle web server and RTK clients, check for RTCM messages, etc.
void loop() {
  static unsigned long lastDisplayUpdate = 0;
  static unsigned long lastStatusLog = 0;
  static unsigned long lastScreenLogTime = 0;
  static unsigned long lastGnssPoll = 0;
  static unsigned long lastDisplayCacheRefresh = 0;
  unsigned long now = millis();

  if (now - lastGnssPoll >= GNSS_POLL_INTERVAL) {
    lastGnssPoll = now;
    loopRTKController();
    processCallbacks();
    displayRTCM1005();
  }

  if (now - lastDisplayCacheRefresh >= DISPLAY_CACHE_REFRESH_INTERVAL || !gnssDisplayCache.initialized) {
    lastDisplayCacheRefresh = now;
    refreshGNSSDisplayCache();
  }

  server.handleClient();
  handleRTKClients();

  ScreenMode desiredScreen = ((now / SCREEN_SWITCH_INTERVAL) % 2 == 0) ? SCREEN_MAIN : SCREEN_RTCM;
  bool screenChanged = (desiredScreen != currentScreen);
  currentScreen = desiredScreen;

  if (screenChanged) {
    unsigned long delta = (lastScreenLogTime == 0) ? 0 : (now - lastScreenLogTime);
    lastScreenLogTime = now;
    Serial.printf("[SCREEN] t=%lu ms, screen=%s, delta=%lu ms\n",
                  now,
                  (currentScreen == SCREEN_MAIN) ? "MAIN" : "RTCM",
                  delta);
  }

  if (hasAppliedBaseConfiguration() &&
      getCurrentBaseMode() == MODE_SURVEY_IN &&
      now - lastStatusLog >= SURVEY_STATUS_LOG_INTERVAL) {
    lastStatusLog = now;
    checkRTKStatus();
  }

  if (screenChanged || now - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL) {
    lastDisplayUpdate = now;

    if (currentScreen == SCREEN_MAIN) {
      displayMainScreen();
    } else {
      displayRTCMScreen();
    }
  }
}
