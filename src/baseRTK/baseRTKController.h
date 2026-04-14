#ifndef BASE_RTK_CONTROLLER_H
#define BASE_RTK_CONTROLLER_H

#include <SparkFun_u-blox_GNSS_v3.h>
#include "wi-fi/wiFiConnectionController.h"

// Expose the GNSS and server if main needs them
extern SFE_UBLOX_GNSS myGNSS;

// Functions main.cpp can call
extern void checkRTKStatus();
extern void initRTKController();
extern bool setMode(BaseConfig config);
extern BaseMode getCurrentBaseMode();
extern bool hasAppliedBaseConfiguration();
extern unsigned long observationTime();
extern float observationAccuracy();
extern bool loopRTKController();
extern unsigned long longitudeData();
extern unsigned long latitudeData();
extern unsigned long altitudeData();
extern u_int8_t fixType();
extern u_int8_t satellites();
extern bool surveyValidity();
extern bool surveyActivity();
extern void processCallbacks();
extern u_int8_t getRTCM1005(RTCM_1005_data_t* data);

#endif