#ifndef WIFI_CONNECTION_CONTROLLER_H
#define WIFI_CONNECTION_CONTROLLER_H

#include <WebServer.h>
#include <SparkFun_u-blox_GNSS_v3.h>

//Enums and Structs
enum BaseMode {
  MODE_SURVEY_IN,
  MODE_FIXED,
  MODE_INVALID
};

struct BaseConfig {
  BaseMode mode;
  float accuracy;
  uint32_t duration;
  double latitude;
  double longitude;
  double altitude;
};

//External objects 
extern WebServer server;
extern SFE_UBLOX_GNSS myGNSS;
extern WiFiClient rtkClient;
extern WiFiServer rtkServer;

//Functions to be used in main.cpp
void initWiFiServer();
void handleRoot();
void handleClient();
void handleRTKClients();
void sendRTCMToClients(uint8_t *data, uint16_t length);
BaseConfig getBaseConfiguration();

#endif