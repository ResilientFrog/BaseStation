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
static bool baseConfigurationApplied = false;

BaseMode getCurrentBaseMode() {
  return currentBaseMode;
}

bool hasAppliedBaseConfiguration() {
  return baseConfigurationApplied;
}

void checkRTKStatus() {
  if (!baseConfigurationApplied || currentBaseMode != MODE_SURVEY_IN) {
    return;
  }

  byte fixType = myGNSS.getFixType(); 
  byte SIV = myGNSS.getSIV();
  int32_t latitudeRaw = myGNSS.getLatitude();
  int32_t longitudeRaw = myGNSS.getLongitude();
  int32_t altitudeRaw = myGNSS.getAltitude();

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
    float latitude = latitudeRaw / 1e7;
    float longitude = longitudeRaw / 1e7;
    float altitude = altitudeRaw / 1000.0;
    logger.logData("RTK_STATUS", latitude, longitude, altitude, fixType, SIV);
  }
}

void initRTKController() {
  Wire.begin();

  if (myGNSS.begin() == false) {
    logger.logError("RTK", "GNSS not detected. Check I2C wiring.");
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
  baseConfigurationApplied = false;

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
      currentBaseMode = MODE_SURVEY_IN;
      baseConfigurationApplied = true;
      result = true;
    } else {
      result = myGNSS.enableSurveyMode(config.duration, config.accuracy, VAL_LAYER_RAM);
      if (result) {
        String msg = "Survey-In started - Duration: " + String(config.duration) + ", Accuracy: " + String(config.accuracy, 2);
        logger.logInfo("RTK", msg);

        currentBaseMode = MODE_SURVEY_IN;
        baseConfigurationApplied = true;
      } else {
        baseConfigurationApplied = false;
        logger.logError("RTK", "Survey-In start failed");
      }
      Serial.println(result ? F("Survey-In started") : F("Survey-In start failed"));
    }
    return result;
  } else if (config.mode == MODE_FIXED) {
    myGNSS.newCfgValset();
    myGNSS.addCfgValset(UBLOX_CFG_TMODE_MODE, 2);
    int32_t latFixed = (int32_t)(config.latitude * 10000000);
    int32_t lonFixed = (int32_t)(config.longitude * 10000000);
    int32_t altFixed = (int32_t)(config.altitude * 1000);
    myGNSS.addCfgValset(UBLOX_CFG_TMODE_LAT, latFixed);
    myGNSS.addCfgValset(UBLOX_CFG_TMODE_LON, lonFixed);
    myGNSS.addCfgValset(UBLOX_CFG_TMODE_HEIGHT, altFixed);
    
    String msg = "Fixed position set - Lat: " + String(config.latitude, 8) + ", Lon: " + String(config.longitude, 8) + ", Alt: " + String(config.altitude, 2);
    logger.logInfo("RTK", msg);

    bool sendOk = myGNSS.sendCfgValset();
    if (sendOk) {
      logger.logData("MODE_FIXED_TRIGGER", (float)config.latitude, (float)config.longitude,
                     (float)config.altitude, myGNSS.getFixType(), myGNSS.getSIV());
      currentBaseMode = MODE_FIXED;
      baseConfigurationApplied = true;
    } else {
      baseConfigurationApplied = false;
    }

    return sendOk;
  }

  return false;
}

unsigned long observationTime(){
  return myGNSS.getSurveyInObservationTime();
}

float observationAccuracy(){
  return myGNSS.getSurveyInMeanAccuracy();
}

bool loopRTKController() {
 return myGNSS.checkUblox();
 
}

unsigned long longitudeData() {
 return myGNSS.getLongitude();
 
}
unsigned long  latitudeData() {
 return  myGNSS.getLatitude();
 
}
unsigned long altitudeData() {
 return myGNSS.getAltitude();
 
}
u_int8_t fixType() {
 return myGNSS.getFixType();
 
}
u_int8_t satellites() {
 return myGNSS.getSIV();
 
}
bool surveyValidity() {
 return myGNSS.getSurveyInValid();
 
}
bool surveyActivity() {
 return myGNSS.getSurveyInActive();
 
}
void processCallbacks() {
  myGNSS.checkCallbacks();
}
u_int8_t getRTCM1005(RTCM_1005_data_t* data) {
 
  return myGNSS.getLatestRTCM1005(data);
}