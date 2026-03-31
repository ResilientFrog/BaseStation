#include <SparkFun_u-blox_GNSS_v3.h>
#include "baseRTKController.h"
#include <Wire.h>
#include "wi-fi/wiFiConnectionController.h"
#include "logger/Logger.h"

SFE_UBLOX_GNSS myGNSS;

class RTCMForwarder : public Print {
public:
  size_t write(uint8_t incoming) override {
    sendRTCMToClients(&incoming, 1);
    return 1;
  }

  size_t write(const uint8_t *buffer, size_t size) override {
    if (buffer == nullptr || size == 0) {
      return 0;
    }

    sendRTCMToClients(const_cast<uint8_t *>(buffer), static_cast<uint16_t>(size));
    return size;
  }
};

RTCMForwarder rtcmForwarder;
static BaseMode currentBaseMode = MODE_INVALID;

BaseMode getCurrentBaseMode() {
  return currentBaseMode;
}

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
  
  // Log data (Fix)
  if (fixType >= 2) {
    float latitude = myGNSS.getLatitude() / 1e7;
    float longitude = myGNSS.getLongitude() / 1e7;
    float altitude = myGNSS.getAltitude() / 1000.0;
    logger.logData("RTK_STATUS", latitude, longitude, altitude, fixType, SIV);
  }
}

void initRTKController() {
  Wire.begin();

  if (myGNSS.begin() == false) {
    logger.logError("RTK", "GNSS not detected. Check I2C wiring.");
    Serial.println(F("GNSS not detected. Check I2C wiring."));
    while (1);
  }

  logger.logInfo("RTK", "GNSS initialized successfully");
  myGNSS.setRTCMOutputPort(rtcmForwarder);
  logger.logInfo("RTK", "RTCM output routed to rover stream clients");

  // Start in disabled timing mode so Survey-In only begins after an explicit web command.
  myGNSS.newCfgValset();
  myGNSS.addCfgValset(UBLOX_CFG_TMODE_MODE, 0);
  if (!myGNSS.sendCfgValset()) {
    logger.logWarn("RTK", "Failed to clear timing mode at startup");
  } else {
    logger.logInfo("RTK", "Timing mode reset to DISABLED at startup");
  }
  currentBaseMode = MODE_INVALID;

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
  if (config.mode == MODE_INVALID) {
    logger.logWarn("RTK", "setMode called with MODE_INVALID");
    return false;
  }

  bool result = true;

  if (config.mode == MODE_SURVEY_IN) {
    if (myGNSS.getSurveyInActive() == true) {
      logger.logInfo("RTK", "Survey-In already active");
      Serial.println(F("Survey-In already active"));
      currentBaseMode = MODE_SURVEY_IN;
      result = true;
    } else {
      result = myGNSS.enableSurveyMode(config.duration, config.accuracy, VAL_LAYER_RAM);
      if (result) {
        String msg = "Survey-In started - Duration: " + String(config.duration) + ", Accuracy: " + String(config.accuracy, 2);
        logger.logInfo("RTK", msg);
        currentBaseMode = MODE_SURVEY_IN;
      } else {
        logger.logError("RTK", "Survey-In start failed");
      }
      Serial.println(result ? F("Survey-In started") : F("Survey-In start failed"));
    }
    return result;
  } else if (config.mode == MODE_FIXED) {
    myGNSS.newCfgValset();
    myGNSS.addCfgValset(UBLOX_CFG_TMODE_MODE, 2); // Fixed Position
    int32_t latFixed = (int32_t)(config.latitude * 10000000);
    int32_t lonFixed = (int32_t)(config.longitude * 10000000);
    int32_t altFixed = (int32_t)(config.altitude * 1000); // meters to mm
    myGNSS.addCfgValset(UBLOX_CFG_TMODE_LAT, latFixed);
    myGNSS.addCfgValset(UBLOX_CFG_TMODE_LON, lonFixed);
    myGNSS.addCfgValset(UBLOX_CFG_TMODE_HEIGHT, altFixed);
    
    String msg = "Fixed position set - Lat: " + String(config.latitude, 7) + ", Lon: " + String(config.longitude, 7) + ", Alt: " + String(config.altitude, 2);
    logger.logInfo("RTK", msg);

    bool sendOk = myGNSS.sendCfgValset();
    if (!sendOk) {
      logger.logError("RTK", "sendCfgValset failed");
      Serial.println(F("sendCfgValset failed"));
    }

    if (sendOk) {
      currentBaseMode = MODE_FIXED;
    }

    return sendOk;
  }

  return false;
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
  sendRTCMToClients(&incoming, 1);
}