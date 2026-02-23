#include "wi-fi/wiFiConnectionController.h"
#include "baseRTK/baseRTKController.h"
#include "logger/Logger.h"
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <LittleFS.h>

#define LOG_FILE "/sensor_log.txt"
const uint32_t BAUD_RATE = 9600;

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

// Screen modes for rotating display
enum ScreenMode {
  SCREEN_MAIN = 0,
  SCREEN_RTCM = 1
};

ScreenMode currentScreen = SCREEN_MAIN;
unsigned long lastScreenSwitch = 0;
const unsigned long SCREEN_SWITCH_INTERVAL = 5000; // Switch screens every 5 seconds

// RTCM message counters
struct RTCMStats {
  uint32_t rtcm1005Count = 0;
  uint32_t rtcm1074Count = 0;
  uint32_t rtcm1084Count = 0;
  uint32_t rtcm1094Count = 0;
  uint32_t rtcm1124Count = 0;
  uint32_t lastUpdate = 0;
};

RTCMStats rtcmStats;


void logToFile(String message) {
  File file = LittleFS.open(LOG_FILE, FILE_APPEND);
  if (!file) {
    Serial.println("Failed to open file");
    return;
  }

  file.println(String(millis()) + " - " + message);
  file.close();
}

// Display RTCM 1005 message on screen
void displayRTCM1005() {
  RTCM_1005_data_t data;
  uint8_t result = myGNSS.getLatestRTCM1005(&data);
  
  if (result == 2) { // Fresh data available
    rtcmStats.rtcm1005Count++;
    rtcmStats.lastUpdate = millis();
    
    double x = data.AntennaReferencePointECEFX;
    x /= 10000.0; // Convert to m
    double y = data.AntennaReferencePointECEFY;
    y /= 10000.0;
    double z = data.AntennaReferencePointECEFZ;
    z /= 10000.0;

    Serial.print(F("RTCM 1005: X="));
    Serial.print(x, 2);
    Serial.print(F(" Y="));
    Serial.print(y, 2);
    Serial.print(F(" Z="));
    Serial.println(z, 2);
    
    logger.logRTCMMessage(1005, rtcmStats.rtcm1005Count);
  }
}

// Check for RTCM 1074 (GPS observations)
void checkRTCM1074() {
  // RTCM 1074 is processed via checkCallbacks() and processRTCM()
  // Counters are maintained in processRTCM buffer handling
  rtcmStats.rtcm1074Count++;
}

// Check for RTCM 1084 (GLONASS observations)
void checkRTCM1084() {
  rtcmStats.rtcm1084Count++;
}

// Check for RTCM 1094 (Galileo observations)
void checkRTCM1094() {
  rtcmStats.rtcm1094Count++;
}

// Check for RTCM 1124 (BeiDou observations)
void checkRTCM1124() {
  rtcmStats.rtcm1124Count++;
}

// Display all available RTCM messages on screen
void displayRTCMScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  display.setCursor(0, 0);
  display.println("RTCM Messages:");
  
  display.setCursor(0, 10);
  display.printf("1005: %lu", rtcmStats.rtcm1005Count);
  
  display.setCursor(0, 20);
  display.printf("1074: %lu", rtcmStats.rtcm1074Count);
  
  display.setCursor(0, 30);
  display.printf("1084: %lu", rtcmStats.rtcm1084Count);
  
  display.setCursor(0, 40);
  display.printf("1094: %lu", rtcmStats.rtcm1094Count);
  
  display.setCursor(0, 50);
  display.printf("1124: %lu", rtcmStats.rtcm1124Count);
  
  display.display();
}

// Display main status screen
void displayMainScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  float accMeters = myGNSS.getSurveyInMeanAccuracy();
  display.setCursor(0, 0);
  display.printf("Acc: %.2f m", accMeters);

  display.setCursor(0, 15);
  display.printf("Fix: %d  Sats: %d", myGNSS.getFixType(), myGNSS.getSIV());

  display.setCursor(0, 30);
  if (myGNSS.getSurveyInValid()) {
    display.print("Survey: DONE");
    display.setCursor(0, 45);
    display.print("RTCM 1005 OK");
  } else if (myGNSS.getSurveyInActive()) {
    display.printf("Survey: ACTIVE (%ds)", myGNSS.getSurveyInObservationTime());
  } else {
    display.print("Survey: INACTIVE");
  }

  display.display();
}


void setup() {
  Serial.begin(BAUD_RATE);
  delay(2000);

  logger.logInfo("System", "Initializing BaseStation");

  Serial.println("===== CHIP INFO =====");
  Serial.println(ESP.getChipModel());
  logger.logInfo("System", "Chip: " + String(ESP.getChipModel()));

  
  SPI.begin(OLED_CLK, -1, OLED_MOSI, OLED_CS);
  SPI.setFrequency(1000000); // safer 1 MHz

  if (!display.begin(SSD1306_SWITCHCAPVCC)) {
    Serial.println("SSD1306 init failed");
    logger.logError("Display", "SSD1306 initialization failed");
    while (true);
  }

  display.clearDisplay();
  display.setTextSize(0.5);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("OLED OK");
  display.display();

  delay(1500);
  // Initialize LittleFS for logging
  if (!LittleFS.begin()) {
    Serial.println(F("LittleFS mount failed"));
    logger.logError("System", "LittleFS mount failed");
  } else {
    Serial.println(F("LittleFS mounted"));
    logger.initialize();  // Initialize logger after filesystem is ready
    logger.logInfo("System", "LittleFS mounted successfully");
    logToFile("System startup");
  }

  initWiFiServer();
  initRTKController();
  
  logger.logInfo("System", "Retrieving base configuration");
  BaseConfig values = getBaseConfiguration();
  Serial.println("Initial Config:");
  Serial.println("Mode: " + String(values.mode == MODE_SURVEY_IN ? "Survey-In" : "Fixed"));
  Serial.println("Accuracy: " + String(values.accuracy));
  
  bool applied = setMode(values);
  String initMsg = applied ? "Initial settings applied successfully" : "Failed to apply initial settings";
  logger.logInfo("System", initMsg);
  Serial.println(applied ? "Initial settings applied" : "Failed to apply initial settings");
}

void loop() {

  // This reads I2C and updates internal variables like getSurveyInValid()
  myGNSS.checkUblox(); 
  Serial.println(myGNSS.checkUblox());
  myGNSS.checkCallbacks(); // Check for new RTCM bytes

  // 2. Handle WiFi/Web logic
  server.handleClient();
  handleRTKClients();

  // 3. Check for RTCM messages
  displayRTCM1005();
  checkRTCM1074();
  checkRTCM1084();
  checkRTCM1094();
  checkRTCM1124();

  // 4. Update Display (rotate between main and RTCM screens)
  static unsigned long lastDisplayUpdate = 0;
  if (millis() - lastDisplayUpdate > 1000) { // Update screen every 1 second
    lastDisplayUpdate = millis();

    // Switch screen every 5 seconds
    if (millis() - lastScreenSwitch > SCREEN_SWITCH_INTERVAL) {
      lastScreenSwitch = millis();
      currentScreen = (ScreenMode)((int)currentScreen + 1);
      if (currentScreen > SCREEN_RTCM) {
        currentScreen = SCREEN_MAIN;
      }
    }

    // Display appropriate screen
    if (currentScreen == SCREEN_MAIN) {
      displayMainScreen();
    } else {
      displayRTCMScreen();
    }

    // -- DEBUG PRINT --
    if (myGNSS.isConnected() == false) {
       Serial.println(F("GNSS Connection Lost!"));
    }
  }
}
