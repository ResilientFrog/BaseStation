#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>
#include <LittleFS.h>
#include <vector>

// Log entry structure
struct LogEntry {
  unsigned long timestamp;
  String level;      // "INFO", "DEBUG", "ERROR", "WARN"
  String component;  // "RTK", "WiFi", "System", etc.
  String message;
};

// Data log entry structure
struct DataLogEntry {
  unsigned long timestamp;
  String dataType;   // "RTCM1005", "RTCM1074", "RTK_STATUS", etc.
  double latitude;
  double longitude;
  float altitude;
  uint8_t fixType;
  uint8_t satellites;
};

class Logger {
private:
  static const char* STEP_LOG_FILE;
  static const char* DATA_LOG_FILE;
  static const int MAX_ENTRIES_IN_MEMORY = 100;
  
  std::vector<LogEntry> stepLogs;
  std::vector<DataLogEntry> dataLogs;
  bool initialized = false;
  
public:
  Logger();
  
  // Initialize logger after LittleFS is mounted
  void initialize();
  bool isInitialized() const { return initialized; }
  
  
  // Step logging
  void logStep(const String& component, const String& message, const String& level = "INFO");
  void logInfo(const String& component, const String& message);
  void logDebug(const String& component, const String& message);
  void logError(const String& component, const String& message);
  void logWarn(const String& component, const String& message);
  
  // Data logging
  void logData(const String& dataType, double latitude, double longitude, float altitude,
               uint8_t fixType, uint8_t satellites);
  void logRTCMMessage(uint16_t messageType, uint32_t count);
  
  // Retrieval
  String getStepLogsAsJSON();
  String getDataLogsAsJSON();
  String getFullLogsAsJSON();
  
  // File operations
  void saveLogs();
  void clearLogs();
  void loadLogsFromFile();
  
  // Statistics
  String getStatistics();
};

extern Logger logger;

#endif
