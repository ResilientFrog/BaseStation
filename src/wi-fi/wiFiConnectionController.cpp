#include <WebServer.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <SparkFun_u-blox_GNSS_v3.h>
#include "wiFiConnectionController.h"
#include "baseRTK/baseRTKController.h"
#include "logger/Logger.h"
#include <ArduinoJson.h>

// Optional local secrets (add include/secrets.h with WIFI_AP_SSID/WIFI_AP_PASSWORD)
#if __has_include("secrets.h")
#include "secrets.h"
#endif

#ifndef WIFI_AP_SSID
#define WIFI_AP_SSID "BASE_STATION_AP"
#endif

#ifndef WIFI_AP_PASSWORD
#define WIFI_AP_PASSWORD "BASE_STATION_PASSWORD"
#endif

#ifndef WIFI_AP_CHANNEL
#define WIFI_AP_CHANNEL 6
#endif

#ifndef WIFI_AP_HIDDEN
#define WIFI_AP_HIDDEN 0
#endif

#ifndef WIFI_AP_MAX_CONNECTIONS
#define WIFI_AP_MAX_CONNECTIONS 4
#endif

const char *AP_SSID     = WIFI_AP_SSID;
const char *AP_PASSWORD = WIFI_AP_PASSWORD;
static BaseConfig lastAppliedConfig = {MODE_INVALID, 2.0f, 60, 0.0, 0.0, 0.0};
static bool hasLastAppliedConfig = false;
static String lastConfigSummary = "No base configuration applied yet.";

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Base Station Control</title>
  <meta name='viewport' content='width=device-width, initial-scale=1'>
  <style>
    body { font-family: Arial; margin: 20px; line-height: 1.6; background: #f0f0f0; }
    .container { max-width: 600px; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 0 10px rgba(0,0,0,0.1); margin-bottom: 20px; }
    .input-group { margin-bottom: 15px; }
    label { display: block; font-weight: bold; }
    input[type='text'], input[type='number'], select { width: 100%; padding: 8px; box-sizing: border-box; border: 1px solid #ccc; border-radius: 4px; }
    input[type='submit'], button { background: #4CAF50; color: white; border: none; padding: 10px; cursor: pointer; width: 100%; border-radius: 4px; font-size: 16px; margin-top: 10px; }
    input[type='submit']:hover, button:hover { background: #45a049; }
    .hidden { display: none; }
    h2 { color: #333; }
    .tab-buttons { display: flex; gap: 10px; margin-bottom: 20px; }
    .tab-button { flex: 1; padding: 10px; background: #ddd; border: none; cursor: pointer; border-radius: 4px; }
    .tab-button.active { background: #4CAF50; color: white; }
    .log-container { max-height: 400px; overflow-y: auto; background: #f9f9f9; padding: 10px; border-radius: 4px; font-family: monospace; font-size: 12px; }
    .log-entry { padding: 5px; border-bottom: 1px solid #eee; }
  </style>
</head>
<body>
  <div class='container'>
    <h2>Base Station Control</h2>
    <form action='/' method='POST' id='configForm'>
      
      <div class='input-group'>
        <label>Mode:</label>
        <select name='mode' id='modeSelect' onchange='toggleFields()'>
          <option value='Survey' %SURVEY_SELECTED%>Survey-In</option>
          <option value='Fixed' %FIXED_SELECTED%>Fixed Position</option>
        </select>
      </div>

      <!-- Survey-In Fields -->
      <div id='surveyFields'>
        <div class='input-group'>
          <label>Accuracy (meters):</label>
          <input type='number' step='0.1' name='acc' value='%ACC_VALUE%'>
        </div>
        <div class='input-group'>
          <label>Minimum Time (seconds):</label>
          <input type='number' name='time' value='%TIME_VALUE%'>
        </div>
      </div>

      <!-- Fixed Position Fields -->
      <div id='fixedFields' class='hidden'>
        <div class='input-group'>
          <label>Latitude:</label>
          <input type='text' name='lat' value='%LAT_VALUE%' placeholder='e.g. 45.1234567'>
        </div>
        <div class='input-group'>
          <label>Longitude:</label>
          <input type='text' name='lon' value='%LON_VALUE%' placeholder='e.g. -75.1234567'>
        </div>
        <div class='input-group'>
          <label>Altitude (m):</label>
          <input type='text' name='alt' value='%ALT_VALUE%' placeholder='e.g. 150.5'>
        </div>
      </div>

      <input type='submit' value='Apply Settings'>
    </form>
  </div>

  <div class='container'>
    <h2>System Logs</h2>
    <div class='tab-buttons'>
      <button type='button' class='tab-button active' data-type='data' onclick='switchTab("data")'>Data</button>
      <button type='button' class='tab-button' data-type='steps' onclick='switchTab("steps")'>Steps</button>
      <button type='button' class='tab-button' data-type='stats' onclick='switchTab("stats")'>Stats</button>
    </div>
    <div class='log-container' id='logContainer'>
      Loading logs...
    </div>
    <div style='display:flex; gap:10px;'>
      <button type='button' onclick='downloadLogs()' id='downloadBtn' style='background: #2196F3;'>Download Logs</button>
      <button type='button' onclick='clearLogs()' style='background: #f44336;'>Clear Logs</button>
    </div>
  </div>

  <script>
    function toggleFields() {
      var mode = document.getElementById('modeSelect').value;
      document.getElementById('surveyFields').classList.toggle('hidden', mode !== 'Survey');
      document.getElementById('fixedFields').classList.toggle('hidden', mode !== 'Fixed');
    }

    var currentTab = 'data';

    function switchTab(type) {
      currentTab = type;
      document.querySelectorAll('.tab-button').forEach(function(btn) {
        btn.classList.toggle('active', btn.dataset.type === type);
      });
      document.getElementById('downloadBtn').style.display = (type === 'stats') ? 'none' : '';
      loadLogs(type);
    }

    function loadLogs(type) {
      let endpoint = '/logs';
      if (type === 'steps') endpoint = '/logs/steps';
      else if (type === 'data') endpoint = '/logs/data';
      else if (type === 'stats') endpoint = '/logs/stats';
      
      fetch(endpoint)
        .then(response => response.json())
        .then(data => {
          const container = document.getElementById('logContainer');
          if (type === 'stats') {
            container.innerHTML = '<pre>' + JSON.stringify(data, null, 2) + '</pre>';
          } else {
            let html = '';
            if (Array.isArray(data)) {
              const filtered = data.filter(entry => {
                if (type === 'steps') {
                  return entry && entry.level !== undefined && entry.component !== undefined && entry.message !== undefined;
                }
                if (type === 'data') {
                  return entry && entry.dataType !== undefined;
                }
                return true;
              });

              filtered.forEach(entry => {
                html += '<div class="log-entry">' + JSON.stringify(entry) + '</div>';
              });

              if (filtered.length === 0) {
                html = '<div class="log-entry">No ' + type + ' logs available.</div>';
              }
            } else {
              html = '<div class="log-entry">' + JSON.stringify(data, null, 2) + '</div>';
            }
            container.innerHTML = html;
          }
        })
        .catch(err => {
          document.getElementById('logContainer').innerHTML = 'Error: ' + err;
        });
    }

    function downloadLogs() {
      if (currentTab === 'steps') {
        window.location.href = '/logs/steps/download';
      } else if (currentTab === 'data') {
        window.location.href = '/logs/data/download';
      }
    }

    function clearLogs() {
      if (confirm('Are you sure you want to clear all logs?')) {
        fetch('/logs/clear', { method: 'POST' })
          .then(() => loadLogs(currentTab))
          .catch(err => alert('Error clearing logs: ' + err));
      }
    }

    // Load logs on page load
    toggleFields();
    loadLogs('data');
    // Refresh logs every 5 seconds
    setInterval(() => loadLogs(currentTab), 5000);
  </script>
</body>
</html>
)rawliteral";

WiFiServer rtkServer(2101);
WebServer server(80);
WiFiClient rtkClient;

static size_t streamToClient(WiFiClient &client, const uint8_t *data, uint16_t length, const char *name) {
  if (!(client && client.connected())) {
    return 0;
  }

  size_t written = client.write(data, length);
  if (written != length) {
    logger.logWarn("WiFi", String("RTCM write failed for ") + name + String(" (") + String(written) + String("/") + String(length) + String(")"));
    Serial.printf("[RTCM] Write failed for %s (%u/%u). Closing client.\n", name, (unsigned int)written, (unsigned int)length);
    client.stop();
  }

  return written;
}

void handleRoot() {
  Serial.println("Client requested root page");

  String page = String(index_html);
  bool fixedMode = hasLastAppliedConfig && lastAppliedConfig.mode == MODE_FIXED;

  page.replace("%SURVEY_SELECTED%", fixedMode ? "" : "selected");
  page.replace("%FIXED_SELECTED%", fixedMode ? "selected" : "");

  String accValue = "2.0";
  String timeValue = "60";
  String latValue = "";
  String lonValue = "";
  String altValue = "";

  if (hasLastAppliedConfig) {
    if (lastAppliedConfig.mode == MODE_SURVEY_IN) {
      accValue = String(lastAppliedConfig.accuracy, 2);
      timeValue = String(lastAppliedConfig.duration);
    } else if (lastAppliedConfig.mode == MODE_FIXED) {
      latValue = String(lastAppliedConfig.latitude, 8);
      lonValue = String(lastAppliedConfig.longitude, 8);
      altValue = String(lastAppliedConfig.altitude, 2);
    }
  }

  page.replace("%ACC_VALUE%", accValue);
  page.replace("%TIME_VALUE%", timeValue);
  page.replace("%LAT_VALUE%", latValue);
  page.replace("%LON_VALUE%", lonValue);
  page.replace("%ALT_VALUE%", altValue);
  page.replace("%CONFIG_STATUS%", lastConfigSummary);

  server.send(200, "text/html", page);
}

BaseConfig getBaseConfiguration() {
  BaseConfig config = {};
  config.mode = MODE_INVALID;

  if (!server.hasArg("mode")) {
    return config; 
  }

  String modeStr = server.arg("mode");
  if (modeStr == "Survey") {
    config.mode = MODE_SURVEY_IN;
    config.accuracy = server.hasArg("acc") ? server.arg("acc").toFloat() : 2.0;
    config.duration = server.hasArg("time") ? server.arg("time").toInt() : 60;

  } else if (modeStr == "Fixed") {
    config.mode = MODE_FIXED;
    config.latitude = server.arg("lat").toDouble();
    config.longitude = server.arg("lon").toDouble();
    config.altitude = server.arg("alt").toDouble();
  }

  return config;
}
void handleUpdate() {
  BaseConfig config = getBaseConfiguration();

  if (config.mode == MODE_INVALID) {
    server.send(400, "text/plain", "Invalid or missing parameters");
    return;
  }

  // A new parameter set starts a fresh measurement session.
  logger.clearLogs();
  resetMeasurementSession();
  logger.logInfo("WiFi", "Logs reset after new base configuration");

  if (config.mode == MODE_SURVEY_IN) {
    logger.logInfo("WiFi", "Applying Survey-In configuration");
    Serial.printf("Survey-In request: duration=%u acc=%.2f\n", config.duration, config.accuracy);
  } else if (config.mode == MODE_FIXED) {
    logger.logInfo("WiFi", "Applying Fixed position configuration");
    Serial.printf("Fixed request: lat=%f lon=%f alt=%f\n", config.latitude, config.longitude, config.altitude);
  }

  bool ok = setMode(config);

  if (ok) {
    hasLastAppliedConfig = true;
    lastAppliedConfig = config;

    if (config.mode == MODE_SURVEY_IN) {
      Serial.println(F("Survey-In started (confirmed)") );
      lastConfigSummary = "Survey-In | Accuracy: " + String(config.accuracy, 2) + " m, Time: " + String(config.duration) + " s";
    } else if (config.mode == MODE_FIXED) {
      Serial.println(F("Fixed position configuration sent"));
      lastConfigSummary = "Fixed | Lat: " + String(config.latitude, 8) + ", Lon: " + String(config.longitude, 8) + ", Alt: " + String(config.altitude, 2) + " m";
    }
    server.send(200, "text/html", "<h1>Settings Applied</h1><a href='/'>Back</a>");
  } else {
    Serial.println(F("Applying configuration failed"));
    lastConfigSummary = "Failed to apply latest settings";
    server.send(500, "text/html", "<h1>Failed to apply settings</h1><a href='/'>Back</a>");
  }
}
void handleClient() {
  server.handleClient();
}
void handleLogs() {
  server.send(200, "application/json", logger.getFullLogsAsJSON());
}

void handleStepLogs() {
  server.send(200, "application/json", logger.getStepLogsAsJSON());
}

void handleDataLogs() {
  server.send(200, "application/json", logger.getDataLogsAsJSON());
}

void handleStats() {
  JsonDocument doc;
  doc["logs"] = serialized(logger.getStatistics());
  doc["storageReady"] = logger.isInitialized();
  JsonObject wifi = doc["wifi"].to<JsonObject>();
  wifi["ip"] = WiFi.softAPIP().toString();
  wifi["clients"] = WiFi.softAPgetStationNum();
  String output;
  serializeJson(doc, output);
  server.send(200, "application/json", output);
}

void handleClearLogs() {
  if (server.method() == HTTP_POST) {
    logger.clearLogs();
    logger.logInfo("WiFi", "Logs cleared by user");
    server.send(200, "application/json", "{\"status\":\"cleared\"}");
  } else {
    server.send(405, "text/plain", "Method not allowed");
  }
}

void handleDownloadStepLogs() {
  File file = LittleFS.open("/logs/steps.txt", FILE_READ);
  if (!file) {
    server.send(404, "text/plain", "No step logs found");
    return;
  }
  server.sendHeader("Content-Disposition", "attachment; filename=\"steps.txt\"");
  server.streamFile(file, "text/plain");
  file.close();
}

void handleDownloadDataLogs() {
  File file = LittleFS.open("/logs/data.txt", FILE_READ);
  if (!file) {
    server.send(404, "text/plain", "No data logs found");
    return;
  }
  server.sendHeader("Content-Disposition", "attachment; filename=\"data.txt\"");
  server.streamFile(file, "text/plain");
  file.close();
}

void initWiFiServer() {
  // Setup WiFi radio for stronger and more stable AP signal
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  bool txPowerSet = WiFi.setTxPower(WIFI_POWER_19_5dBm);

  // Setup WiFi Access Point
  bool apStarted = WiFi.softAP(AP_SSID, AP_PASSWORD, WIFI_AP_CHANNEL, WIFI_AP_HIDDEN, WIFI_AP_MAX_CONNECTIONS);
  delay(100);

  if (!txPowerSet) {
    logger.logWarn("WiFi", "Failed to set TX power to max");
  }

  if (!apStarted) {
    logger.logError("WiFi", "Failed to start WiFi AP");
  } else {
    logger.logInfo("WiFi", "AP channel: " + String(WIFI_AP_CHANNEL) + ", max clients: " + String(WIFI_AP_MAX_CONNECTIONS));
    logger.logInfo("WiFi", String("AP IP: ") + WiFi.softAPIP().toString());
  }
  
  logger.logInfo("WiFi", "WiFi AP initialized");
  
  server.on("/", HTTP_GET, handleRoot);
  server.on("/", HTTP_POST, handleUpdate);
  server.on("/logs", HTTP_GET, handleLogs);
  server.on("/logs/steps", HTTP_GET, handleStepLogs);
  server.on("/logs/data", HTTP_GET, handleDataLogs);
  server.on("/logs/stats", HTTP_GET, handleStats);
  server.on("/logs/clear", HTTP_POST, handleClearLogs);
  server.on("/logs/steps/download", HTTP_GET, handleDownloadStepLogs);
  server.on("/logs/data/download", HTTP_GET, handleDownloadDataLogs);

  rtkServer.begin();
  rtkServer.setNoDelay(true);
  
  server.begin();
  logger.logInfo("WiFi", "Web server started on port 80");
  logger.logInfo("WiFi", "RTCM TCP server started on port 2101");
 
}

void handleRTKClients() {
  // Check if a new Rover wants to connect
  if (rtkServer.hasClient()) {
    WiFiClient incomingTcp = rtkServer.available();
    if (rtkClient && rtkClient.connected()) {
      Serial.println(F("[RTCM] Replacing existing TCP rover connection"));
      rtkClient.stop();
    }
    rtkClient = incomingTcp;
    Serial.println(F("Rover Connected for RTCM Stream!"));
    Serial.printf("[RTCM] TCP rover connected from %s:%u\n", rtkClient.remoteIP().toString().c_str(), rtkClient.remotePort());
  }

  // Check if Rover disconnected
  if (rtkClient && !rtkClient.connected()) {
    rtkClient.stop();
    Serial.println(F("Rover Disconnected."));
    Serial.println(F("[RTCM] TCP rover stream closed"));
  }
}

// This is the function called by your baseRTKController
void sendRTCMToClients(const uint8_t *data, uint16_t length) {
  static uint32_t tcpBytesWindow = 0;
  static uint32_t packetsWindow = 0;
  static unsigned long lastStatsPrintMs = 0;

  size_t tcpWritten = streamToClient(rtkClient, data, length, "TCP rover");

  tcpBytesWindow += (uint32_t)tcpWritten;
  packetsWindow++;

  unsigned long now = millis();
  if (rtkClient && rtkClient.connected()) {
    if (now - lastStatsPrintMs >= 2000) {
      Serial.printf("[RTCM] 2s stats: TCP=%luB packets=%lu\n", tcpBytesWindow, packetsWindow);
      tcpBytesWindow = 0;
      packetsWindow = 0;
      lastStatsPrintMs = now;
    }
  } else {
    tcpBytesWindow = 0;
    packetsWindow = 0;
    lastStatsPrintMs = now;
  }
}
