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

    sendRTCMToClients(buffer, static_cast<uint16_t>(size));
    return size;
  }
};

RTCMForwarder rtcmForwarder;
static BaseMode currentBaseMode = MODE_INVALID;
static bool baseConfigurationApplied = false;
static bool surveyCoordinatesLogged = false;
static bool fixedCoordinatesLogged = false;

namespace {
bool configureRtcmOutput() {
  bool ok = true;

  myGNSS.setRTCMOutputPort(rtcmForwarder);
  ok &= myGNSS.setI2COutput(COM_TYPE_UBX | COM_TYPE_RTCM3);

  if (!myGNSS.newCfgValset()) {
    logger.logError("RTK", "Failed to start CFG-VALSET for RTCM config");
    return false;
  }

  ok &= myGNSS.addCfgValset(UBLOX_CFG_MSGOUT_RTCM_3X_TYPE1005_I2C, 1);
  ok &= myGNSS.addCfgValset(UBLOX_CFG_MSGOUT_RTCM_3X_TYPE1074_I2C, 1);
  ok &= myGNSS.addCfgValset(UBLOX_CFG_MSGOUT_RTCM_3X_TYPE1084_I2C, 1);
  ok &= myGNSS.addCfgValset(UBLOX_CFG_MSGOUT_RTCM_3X_TYPE1094_I2C, 1);
  ok &= myGNSS.addCfgValset(UBLOX_CFG_MSGOUT_RTCM_3X_TYPE1124_I2C, 1);
  ok &= myGNSS.addCfgValset(UBLOX_CFG_MSGOUT_RTCM_3X_TYPE1230_I2C, 10);
  ok &= myGNSS.sendCfgValset();

  if (!ok) {
    logger.logError("RTK", "RTCM output configuration failed");
    return false;
  }

  logger.logInfo("RTK", "RTCM output + message rates configured");
  return true;
}
}

BaseMode getCurrentBaseMode() {
  return currentBaseMode;
}

bool hasAppliedBaseConfiguration() {
  return baseConfigurationApplied;
}

void checkRTKStatus() {
  if (!baseConfigurationApplied) {
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

  String modeStr = (currentBaseMode == MODE_FIXED) ? "Fixed" : "Survey";
  logger.logInfo("RTK", "Mode: " + modeStr + " | Fix: " + fixStr + " | Satellites: " + String(SIV));
  
  double latitude = latitudeRaw / 1e7;
  double longitude = longitudeRaw / 1e7;
  double altitude = altitudeRaw / 1000.0;

  if (currentBaseMode == MODE_SURVEY_IN) {
    // Survey-In: log coordinates, accuracy and observation time every cycle.
    logger.logDataAccuracy("SURVEY_STATUS", latitude, longitude, altitude, fixType, SIV, observationAccuracy(), observationTime());
  } else if (currentBaseMode == MODE_FIXED) {
    // Fixed mode coordinates are recorded only once.
    if (!fixedCoordinatesLogged) {
      logger.logDataAccuracy("FIXED_COORDINATES", latitude, longitude, altitude, fixType, SIV, NAN);
      fixedCoordinatesLogged = true;
      logger.logInfo("RTK", "Fixed coordinates logged once");
    }
  }
}

void initRTKController() {
  Wire.begin();

  if (myGNSS.begin() == false) {
    logger.logError("RTK", "GNSS not detected. Check I2C wiring.");
    while (1);
  }

  logger.logInfo("RTK", "GNSS initialized successfully");
  logger.logInfo("RTK", "GNSS init done, configuring RTCM output pipeline");

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
  surveyCoordinatesLogged = false;
  fixedCoordinatesLogged = false;

  if (!configureRtcmOutput()) {
    logger.logWarn("RTK", "Initial RTCM configuration failed; retry after mode apply");
  }

  myGNSS.saveConfigSelective(VAL_CFG_SUBSEC_IOPORT);
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
        surveyCoordinatesLogged = false;
        fixedCoordinatesLogged = false;
        if (!configureRtcmOutput()) {
          logger.logError("RTK", "Survey mode active but RTCM output setup failed");
          baseConfigurationApplied = false;
          return false;
        }
      } else {
        baseConfigurationApplied = false;
        logger.logError("RTK", "Survey-In start failed");
      }
      Serial.println(result ? F("Survey-In started") : F("Survey-In start failed"));
    }
    return result;
  } else if (config.mode == MODE_FIXED) {
    // setStaticPosition expects: lat in degrees*1e7, lon in degrees*1e7, alt in cm
    int32_t latFixed = (int32_t)(config.latitude  * 10000000.0);
    int32_t lonFixed = (int32_t)(config.longitude * 10000000.0);
    int32_t altFixed = (int32_t)(config.altitude  * 100.0);

    String msg = "Fixed position set - Lat: " + String(config.latitude, 8) + ", Lon: " + String(config.longitude, 8) + ", Alt: " + String(config.altitude, 2);
    logger.logInfo("RTK", msg);

    bool sendOk = myGNSS.setStaticPosition(latFixed, lonFixed, altFixed, true, VAL_LAYER_RAM);
    if (sendOk) {
      surveyCoordinatesLogged = false;
      fixedCoordinatesLogged = false;
      if (!configureRtcmOutput()) {
        logger.logError("RTK", "Fixed mode set but RTCM output setup failed");
        baseConfigurationApplied = false;
        return false;
      }
      logger.logDataAccuracy("FIXED_COORDINATES", config.latitude, config.longitude,
                             config.altitude, myGNSS.getFixType(), myGNSS.getSIV(), NAN);
      fixedCoordinatesLogged = true;
      logger.logInfo("RTK", "Fixed mode active; RTCM output should be available");
      currentBaseMode = MODE_FIXED;
      baseConfigurationApplied = true;
    } else {
      logger.logError("RTK", "Fixed position configuration failed to apply");
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

int32_t longitudeData() {
 return myGNSS.getLongitude();
 
}
int32_t latitudeData() {
 return  myGNSS.getLatitude();
 
}
int32_t altitudeData() {
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