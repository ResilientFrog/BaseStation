#include "Logger.h"
#include <math.h>
#include <ArduinoJson.h>

namespace {
int findNthComma(const String& input, int n) {
  int found = 0;
  for (int i = 0; i < input.length(); ++i) {
    if (input[i] == ',') {
      found++;
      if (found == n) {
        return i;
      }
    }
  }
  return -1;
}

String toCsvNumber(double value, int decimals) {
  if (isnan(value) || isinf(value)) {
    return "";
  }
  return String(value, decimals);
}

double parseCsvNumber(const String& token) {
  if (token.length() == 0) {
    return NAN;
  }
  return token.toDouble();
}
}

const char* Logger::STEP_LOG_FILE = "/logs/steps.txt";
const char* Logger::DATA_LOG_FILE = "/logs/data.txt";

Logger logger;

Logger::Logger() {

}

void Logger::initialize() {
  if (initialized) return;
  
  // Create logs directory if it doesn't exist
  if (!LittleFS.exists("/logs")) {
    LittleFS.mkdir("/logs");
  }
  loadLogsFromFile();
  initialized = true;
}

void Logger::logStep(const String& component, const String& message, const String& level) {
  LogEntry entry;
  entry.timestamp = millis() / 1000UL;
  entry.component = component;
  entry.message = message;
  entry.level = level;
  
  stepLogs.push_back(entry);
  if (stepLogs.size() > MAX_ENTRIES_IN_MEMORY) {
    stepLogs.erase(stepLogs.begin());
  }
  
  // Also log to serial for debugging
  Serial.printf("[%lu] [%s] %s: %s\n", entry.timestamp, level.c_str(), component.c_str(), message.c_str());
  
  // Write to file only if initialized
  if (initialized) {
    File file = LittleFS.open(STEP_LOG_FILE, FILE_APPEND);
    if (file) {
      file.printf("%lu,%s,%s,%s\n", entry.timestamp, level.c_str(), component.c_str(), message.c_str());
      file.close();
    }
  }
}

void Logger::logInfo(const String& component, const String& message) {
  logStep(component, message, "INFO");
}

void Logger::logError(const String& component, const String& message) {
  logStep(component, message, "ERROR");
}

void Logger::logWarn(const String& component, const String& message) {
  logStep(component, message, "WARN");
}

void Logger::logDataAccuracy(const String& dataType, double latitude, double longitude, float altitude,
                             uint8_t fixType, uint8_t satellites, float accuracyMeters,
                             unsigned long observationTimeSec) {
  DataLogEntry entry;
  entry.timestamp = millis() / 1000UL;
  entry.dataType = dataType;
  entry.latitude = latitude;
  entry.longitude = longitude;
  entry.altitude = altitude;
  entry.fixType = fixType;
  entry.satellites = satellites;
  entry.accuracyMeters = accuracyMeters;
  entry.observationTimeSec = observationTimeSec;
  
  dataLogs.push_back(entry);
  if (dataLogs.size() > MAX_ENTRIES_IN_MEMORY) {
    dataLogs.erase(dataLogs.begin());
  }
  
  // Write to file only if initialized
  if (initialized) {
    File file = LittleFS.open(DATA_LOG_FILE, FILE_APPEND);
    if (file) {
      String line = String(entry.timestamp) + "," + entry.dataType + "," +
                    toCsvNumber(entry.latitude, 8) + "," +
                    toCsvNumber(entry.longitude, 8) + "," +
                    toCsvNumber(entry.altitude, 2) + "," +
                    String(entry.fixType) + "," +
                    String(entry.satellites) + "," +
                    toCsvNumber(entry.accuracyMeters, 3) + "," +
                    String(entry.observationTimeSec);
      file.println(line);
      file.close();
    }
  }
}

String Logger::getStepLogsAsJSON() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (const auto& entry : stepLogs) {
    JsonObject obj = arr.add<JsonObject>();
    obj["timestamp"] = entry.timestamp;
    obj["level"] = entry.level;
    obj["component"] = entry.component;
    obj["message"] = entry.message;
  }
  String output;
  serializeJson(doc, output);
  return output;
}

String Logger::getDataLogsAsJSON() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (const auto& entry : dataLogs) {
    JsonObject obj = arr.add<JsonObject>();
    obj["timestamp"] = entry.timestamp;
    obj["dataType"] = entry.dataType;
    obj["latitude"] = entry.latitude;
    obj["longitude"] = entry.longitude;
    obj["altitude"] = (double)entry.altitude;
    obj["fixType"] = entry.fixType;
    obj["satellites"] = entry.satellites;
    obj["accuracyMeters"] = (double)entry.accuracyMeters;
    obj["observationTimeSec"] = entry.observationTimeSec;
  }
  String output;
  serializeJson(doc, output);
  return output;
}

String Logger::getFullLogsAsJSON() {
  JsonDocument doc;
  doc["steps"] = serialized(getStepLogsAsJSON());
  doc["data"] = serialized(getDataLogsAsJSON());
  String output;
  serializeJson(doc, output);
  return output;
}

void Logger::clearLogs() {
  stepLogs.clear();
  dataLogs.clear();
  
  File stepFile = LittleFS.open(STEP_LOG_FILE, FILE_WRITE);
  if (stepFile) {
    stepFile.close();
  }
  
  File dataFile = LittleFS.open(DATA_LOG_FILE, FILE_WRITE);
  if (dataFile) {
    dataFile.close();
  }
  
  logInfo("Logger", "Logs cleared");
}

void Logger::loadLogsFromFile() {
  stepLogs.clear();
  dataLogs.clear();

  if (LittleFS.exists(STEP_LOG_FILE)) {
    File stepFile = LittleFS.open(STEP_LOG_FILE, FILE_READ);
    if (stepFile) {
      while (stepFile.available()) {
        String line = stepFile.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) {
          continue;
        }

        int comma1 = findNthComma(line, 1);
        int comma2 = findNthComma(line, 2);
        int comma3 = findNthComma(line, 3);
        if (comma1 <= 0 || comma2 <= comma1 || comma3 <= comma2) {
          continue;
        }

        LogEntry entry;
        entry.timestamp = (unsigned long)line.substring(0, comma1).toInt();
        entry.level = line.substring(comma1 + 1, comma2);
        entry.component = line.substring(comma2 + 1, comma3);
        entry.message = line.substring(comma3 + 1);
        stepLogs.push_back(entry);
        if (stepLogs.size() > MAX_ENTRIES_IN_MEMORY) {
          stepLogs.erase(stepLogs.begin());
        }
      }
      stepFile.close();
    }
  }

  if (LittleFS.exists(DATA_LOG_FILE)) {
    File dataFile = LittleFS.open(DATA_LOG_FILE, FILE_READ);
    if (dataFile) {
      while (dataFile.available()) {
        String line = dataFile.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) {
          continue;
        }

        int comma1 = findNthComma(line, 1);
        int comma2 = findNthComma(line, 2);
        int comma3 = findNthComma(line, 3);
        int comma4 = findNthComma(line, 4);
        int comma5 = findNthComma(line, 5);
        int comma6 = findNthComma(line, 6);
        int comma7 = findNthComma(line, 7);
        if (comma1 <= 0 || comma2 <= comma1 || comma3 <= comma2 ||
            comma4 <= comma3 || comma5 <= comma4 || comma6 <= comma5) {
          continue;
        }

        DataLogEntry entry;
        entry.timestamp = (unsigned long)line.substring(0, comma1).toInt();
        entry.dataType = line.substring(comma1 + 1, comma2);
        entry.latitude = parseCsvNumber(line.substring(comma2 + 1, comma3));
        entry.longitude = parseCsvNumber(line.substring(comma3 + 1, comma4));
        entry.altitude = (float)parseCsvNumber(line.substring(comma4 + 1, comma5));
        entry.fixType = (uint8_t)line.substring(comma5 + 1, comma6).toInt();
        int comma8 = findNthComma(line, 8);
        if (comma7 > comma6) {
          entry.satellites = (uint8_t)line.substring(comma6 + 1, comma7).toInt();
          entry.accuracyMeters = (float)parseCsvNumber(line.substring(comma7 + 1, comma8 > comma7 ? comma8 : line.length()));
          entry.observationTimeSec = (comma8 > comma7) ? (unsigned long)line.substring(comma8 + 1).toInt() : 0;
        } else {
          entry.satellites = (uint8_t)line.substring(comma6 + 1).toInt();
          entry.accuracyMeters = NAN;
          entry.observationTimeSec = 0;
        }
        dataLogs.push_back(entry);
        if (dataLogs.size() > MAX_ENTRIES_IN_MEMORY) {
          dataLogs.erase(dataLogs.begin());
        }
      }
      dataFile.close();
    }
  }
}

String Logger::getStatistics() {
  JsonDocument doc;
  doc["stepLogsCount"] = stepLogs.size();
  doc["dataLogsCount"] = dataLogs.size();
  String output;
  serializeJson(doc, output);
  return output;
}
