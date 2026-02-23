#ifndef BASE_RTK_CONTROLLER_H
#define BASE_RTK_CONTROLLER_H

#include <SparkFun_u-blox_GNSS_v3.h>
#include "wi-fi/wiFiConnectionController.h"

// Expose the GNSS and server if main needs them
extern SFE_UBLOX_GNSS myGNSS;

// Functions main.cpp can call
void checkRTKStatus();
void initRTKController();
bool setMode(BaseConfig config);
void observationTime();
void observationAccuracy();
void loopRTKController();

#endif