#include <SparkFun_u-blox_GNSS_v3.h>
#include "baseRTKController.h"
#include <Wire.h>
#include "wi-fi/wiFiConnectionController.h"
#include <LittleFS.h>
#include "logger/Logger.h"

uint8_t rtcmBuffer[256];
uint16_t bufferIndex = 0;
SFE_UBLOX_GNSS myGNSS;
const char *LOG_FILE = "/sensor_log.txt";

void checkRTKStatus() {
  byte fixType = myGNSS.getFixType(); 
  byte SIV = myGNSS.getSIV();
  
  String fixStr;
  if (fixType == 0) fixStr = "No Fix";
  else if (fixType == 1) fixStr = "Dead Reckoning";
  else if (fixType == 2) fixStr = "2D Fix";
  else if (fixType == 3) fixStr = "3D Fix";
  else if (fixType == 4) fixStr = "RTK FLOAT";
  else if (fixType == 5) fixStr = "RTK FIXED";
  else fixStr = "Unknown";

  logger.logInfo("RTK", "Fix: " + fixStr + " | Satellites: " + String(SIV));
  
  // Also log data if we have a fix
  if (fixType >= 2) {
    float latitude = myGNSS.getLatitude() / 1e7;
    float longitude = myGNSS.getLongitude() / 1e7;
    float altitude = myGNSS.getAltitude() / 1000.0;
    logger.logData("RTK_STATUS", latitude, longitude, altitude, fixType, SIV);
  }
}

void initRTKController() {
  Serial.begin(9800);
  Wire.begin();

  if (myGNSS.begin() == false) {
    logger.logError("RTK", "GNSS not detected. Check I2C wiring.");
    Serial.println(F("GNSS not detected. Check I2C wiring."));
    while (1);
  }

  logger.logInfo("RTK", "GNSS initialized successfully");

   // Clear previous values
  myGNSS.newCfgValset();
  myGNSS.addCfgValset(UBLOX_CFG_TMODE_MODE, 0);
  myGNSS.sendCfgValset();
  delay(500);

  // Set I2C port to output UBX and RTCM3
  myGNSS.setI2COutput(COM_TYPE_UBX | COM_TYPE_RTCM3); // Ensure RTCM3 is enabled
  myGNSS.saveConfigSelective(VAL_CFG_SUBSEC_IOPORT);
  

  bool response = myGNSS.newCfgValset();
  myGNSS.addCfgValset(UBLOX_CFG_MSGOUT_RTCM_3X_TYPE1005_I2C, 1);
  myGNSS.addCfgValset(UBLOX_CFG_MSGOUT_RTCM_3X_TYPE1074_I2C, 1);
  myGNSS.addCfgValset(UBLOX_CFG_MSGOUT_RTCM_3X_TYPE1084_I2C, 1);
  myGNSS.addCfgValset(UBLOX_CFG_MSGOUT_RTCM_3X_TYPE1094_I2C, 1);
  myGNSS.addCfgValset(UBLOX_CFG_MSGOUT_RTCM_3X_TYPE1124_I2C, 1);
  myGNSS.addCfgValset(UBLOX_CFG_MSGOUT_RTCM_3X_TYPE1230_I2C, 10);
  myGNSS.sendCfgValset();
  
  logger.logInfo("RTK", "RTCM message types configured");
}

bool setMode(BaseConfig config){
  myGNSS.newCfgValset();
  bool result = true;

  if (config.mode == MODE_SURVEY_IN) {
    if (myGNSS.getSurveyInActive() == true) {
      logger.logInfo("RTK", "Survey-In already active");
      Serial.println(F("Survey-In already active"));
      result = true;
    } else {
      result = myGNSS.enableSurveyMode(config.duration, config.accuracy, VAL_LAYER_RAM);
      if (result) {
        String msg = "Survey-In started - Duration: " + String(config.duration) + ", Accuracy: " + String(config.accuracy, 2);
        logger.logInfo("RTK", msg);
      } else {
        logger.logError("RTK", "Survey-In start failed");
      }
      Serial.println(result ? F("Survey-In started") : F("Survey-In start failed"));
    }
  } else if (config.mode == MODE_FIXED) {
    myGNSS.addCfgValset(UBLOX_CFG_TMODE_MODE, 2); // Fixed Position
    int32_t latFixed = (int32_t)(config.latitude * 10000000);
    int32_t lonFixed = (int32_t)(config.longitude * 10000000);
    int32_t altFixed = (int32_t)(config.altitude * 1000); // meters to mm
    myGNSS.addCfgValset(UBLOX_CFG_TMODE_LAT, latFixed);
    myGNSS.addCfgValset(UBLOX_CFG_TMODE_LON, lonFixed);
    myGNSS.addCfgValset(UBLOX_CFG_TMODE_HEIGHT, altFixed);
    
    String msg = "Fixed position set - Lat: " + String(config.latitude, 7) + ", Lon: " + String(config.longitude, 7) + ", Alt: " + String(config.altitude, 2);
    logger.logInfo("RTK", msg);
  }

  // sendCfgValset may be needed to apply changes; combine its result with prior outcome
  bool sendOk = myGNSS.sendCfgValset();
  if (!sendOk) {
    logger.logError("RTK", "sendCfgValset failed");
    Serial.println(F("sendCfgValset failed"));
  }

  return (result && sendOk);
}

void observationTime(){
  myGNSS.getSurveyInObservationTime();
}

void observationAccuracy(){
  myGNSS.getSurveyInMeanAccuracy();
}

void loopRTKController() {
  myGNSS.checkUblox();
}

void processRTCM(uint8_t incoming) {

  //TODO maybe to transfer it to wi-fi part
  if (incoming < 0x10) Serial.print("0");
  Serial.print(incoming, HEX);
  Serial.print(" ");
  
  rtcmBuffer[bufferIndex++] = incoming;
  if (bufferIndex >= 128) {
    //Wi-fi client to send RTCM
    // Send to connected rover clients via WiFi
    sendRTCMToClients(rtcmBuffer, 128);
    
    // Append chunk to log
    File f = LittleFS.open(LOG_FILE, FILE_APPEND);
    if (f) {
      f.write(rtcmBuffer, 128);
      f.close();
      logger.logDebug("RTK", "RTCM chunk sent and logged");
      Serial.println(F("RTCM chunk sent and logged"));
    } else {
      logger.logError("RTK", "Failed to open log file for RTCM");
      Serial.println(F("Failed to open log file for RTCM"));
    }

    bufferIndex = 0;
  }
}