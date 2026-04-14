#include "Logger.h"

namespace {
String escapeJson(const String& input) {
  String escaped;
  escaped.reserve(input.length() + 8);

  for (size_t index = 0; index < input.length(); ++index) {
    char character = input[index];
    switch (character) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        escaped += character;
        break;
    }
  }

  return escaped;
}

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
  entry.timestamp = millis();
  entry.component = component;
  entry.message = message;
  entry.level = level;
  
  stepLogs.push_back(entry);
  
  // Keep only recent entries in memory
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

void Logger::logDebug(const String& component, const String& message) {
  logStep(component, message, "DEBUG");
}

void Logger::logError(const String& component, const String& message) {
  logStep(component, message, "ERROR");
}

void Logger::logWarn(const String& component, const String& message) {
  logStep(component, message, "WARN");
}

void Logger::logData(const String& dataType, double latitude, double longitude, float altitude,
                     uint8_t fixType, uint8_t satellites) {
  DataLogEntry entry;
  entry.timestamp = millis();
  entry.dataType = dataType;
  entry.latitude = latitude;
  entry.longitude = longitude;
  entry.altitude = altitude;
  entry.fixType = fixType;
  entry.satellites = satellites;
  
  dataLogs.push_back(entry);
  
  // Write to file only if initialized
  if (initialized) {
    File file = LittleFS.open(DATA_LOG_FILE, FILE_APPEND);
    if (file) {
      file.printf("%lu,%s,%.8f,%.8f,%.2f,%d,%d\n", entry.timestamp, entry.dataType.c_str(),
                  entry.latitude, entry.longitude, entry.altitude, entry.fixType, entry.satellites);
      file.close();
    }
  }
}

void Logger::logRTCMMessage(uint16_t messageType, uint32_t count) {
  String msg = "RTCM Type " + String(messageType) + " count: " + String(count);
  logInfo("RTK", msg);
}

String Logger::getStepLogsAsJSON() {
  String json = "[";
  for (size_t i = 0; i < stepLogs.size(); i++) {
    if (i > 0) json += ",";
    json += "{";
    json += "\"timestamp\":" + String(stepLogs[i].timestamp) + ",";
    json += "\"level\":\"" + escapeJson(stepLogs[i].level) + "\",";
    json += "\"component\":\"" + escapeJson(stepLogs[i].component) + "\",";
    json += "\"message\":\"" + escapeJson(stepLogs[i].message) + "\"";
    json += "}";
  }
  json += "]";
  return json;
}

String Logger::getDataLogsAsJSON() {
  String json = "[";
  for (size_t i = 0; i < dataLogs.size(); i++) {
    if (i > 0) json += ",";
    json += "{";
    json += "\"timestamp\":" + String(dataLogs[i].timestamp) + ",";
    json += "\"dataType\":\"" + escapeJson(dataLogs[i].dataType) + "\",";
    json += "\"latitude\":" + String(dataLogs[i].latitude, 8) + ",";
    json += "\"longitude\":" + String(dataLogs[i].longitude, 8) + ",";
    json += "\"altitude\":" + String(dataLogs[i].altitude, 2) + ",";
    json += "\"fixType\":" + String(dataLogs[i].fixType) + ",";
    json += "\"satellites\":" + String(dataLogs[i].satellites);
    json += "}";
  }
  json += "]";
  return json;
}

String Logger::getFullLogsAsJSON() {
  String json = "{";
  json += "\"steps\":" + getStepLogsAsJSON() + ",";
  json += "\"data\":" + getDataLogsAsJSON();
  json += "}";
  return json;
}

void Logger::saveLogs() {
  // Logs are saved to file on each call, this is for flushing if needed
  // In LittleFS, file operations are immediate
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
        if (comma1 <= 0 || comma2 <= comma1 || comma3 <= comma2 ||
            comma4 <= comma3 || comma5 <= comma4 || comma6 <= comma5) {
          continue;
        }

        DataLogEntry entry;
        entry.timestamp = (unsigned long)line.substring(0, comma1).toInt();
        entry.dataType = line.substring(comma1 + 1, comma2);
        entry.latitude = line.substring(comma2 + 1, comma3).toDouble();
        entry.longitude = line.substring(comma3 + 1, comma4).toDouble();
        entry.altitude = line.substring(comma4 + 1, comma5).toFloat();
        entry.fixType = (uint8_t)line.substring(comma5 + 1, comma6).toInt();
        entry.satellites = (uint8_t)line.substring(comma6 + 1).toInt();
        dataLogs.push_back(entry);
      }
      dataFile.close();
    }
  }
}

String Logger::getStatistics() {
  String stats = "{";
  stats += "\"stepLogsCount\":" + String(stepLogs.size()) + ",";
  stats += "\"dataLogsCount\":" + String(dataLogs.size());
  stats += "}";
  return stats;
}
