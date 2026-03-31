#include <WebServer.h>
#include <WiFi.h>
#include <SparkFun_u-blox_GNSS_v3.h>
#include "wiFiConnectionController.h"
#include "baseRTK/baseRTKController.h"
#include "logger/Logger.h"

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
          <option value='Survey'>Survey-In</option>
          <option value='Fixed'>Fixed Position</option>
        </select>
      </div>

      <!-- Survey-In Fields -->
      <div id='surveyFields'>
        <div class='input-group'>
          <label>Accuracy (meters):</label>
          <input type='number' step='0.1' name='acc' value='2.0'>
        </div>
        <div class='input-group'>
          <label>Minimum Time (seconds):</label>
          <input type='number' name='time' value='60'>
        </div>
      </div>

      <!-- Fixed Position Fields -->
      <div id='fixedFields' class='hidden'>
        <div class='input-group'>
          <label>Latitude:</label>
          <input type='text' name='lat' placeholder='e.g. 45.1234567'>
        </div>
        <div class='input-group'>
          <label>Longitude:</label>
          <input type='text' name='lon' placeholder='e.g. -75.1234567'>
        </div>
        <div class='input-group'>
          <label>Altitude (m):</label>
          <input type='text' name='alt' placeholder='e.g. 150.5'>
        </div>
      </div>

      <input type='submit' value='Apply Settings'>
    </form>
  </div>

  <div class='container'>
    <h2>System Logs</h2>
    <div class='tab-buttons'>
      <button class='tab-button active' onclick='loadLogs("all")'>All Logs</button>
      <button class='tab-button' onclick='loadLogs("steps")'>Steps</button>
      <button class='tab-button' onclick='loadLogs("data")'>Data</button>
      <button class='tab-button' onclick='loadLogs("stats")'>Stats</button>
    </div>
    <div class='log-container' id='logContainer'>
      Loading logs...
    </div>
    <button onclick='clearLogs()' style='background: #f44336;'>Clear Logs</button>
  </div>

  <script>
    function toggleFields() {
      var mode = document.getElementById('modeSelect').value;
      document.getElementById('surveyFields').classList.toggle('hidden', mode !== 'Survey');
      document.getElementById('fixedFields').classList.toggle('hidden', mode !== 'Fixed');
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
              data.forEach(entry => {
                html += '<div class="log-entry">' + JSON.stringify(entry) + '</div>';
              });
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

    function clearLogs() {
      if (confirm('Are you sure you want to clear all logs?')) {
        fetch('/logs/clear', { method: 'POST' })
          .then(() => loadLogs('all'))
          .catch(err => alert('Error clearing logs: ' + err));
      }
    }

    // Load logs on page load
    loadLogs('all');
    // Refresh logs every 5 seconds
    setInterval(() => loadLogs('all'), 5000);
  </script>
</body>
</html>
)rawliteral";

WiFiServer rtkServer(2101);
WebServer server(80);
WiFiClient rtkClient;

static size_t streamToClient(WiFiClient &client, uint8_t *data, uint16_t length, const char *name) {
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
  server.send(200, "text/html", index_html);
}

BaseConfig getBaseConfiguration() {
  BaseConfig config;
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

  Serial.println(F("Applying configuration from web UI"));
  if (config.mode == MODE_SURVEY_IN) {
    Serial.printf("Survey-In request: duration=%u acc=%.2f\n", config.duration, config.accuracy);
  } else if (config.mode == MODE_FIXED) {
    Serial.printf("Fixed request: lat=%f lon=%f alt=%f\n", config.latitude, config.longitude, config.altitude);
  }

  bool ok = setMode(config);

  if (ok) {
    if (config.mode == MODE_SURVEY_IN) {
      Serial.println(F("Survey-In started (confirmed)") );
    } else if (config.mode == MODE_FIXED) {
      Serial.println(F("Fixed position configuration sent"));
    }
    server.send(200, "text/html", "<h1>Settings Applied</h1><a href='/'>Back</a>");
  } else {
    Serial.println(F("Applying configuration failed"));
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
  String stats = "{";
  stats += "\"logs\":" + logger.getStatistics() + ",";
  stats += "\"storageReady\":" + String(logger.isInitialized() ? "true" : "false") + ",";
  stats += "\"wifi\":{\"ip\":\"" + WiFi.softAPIP().toString() + "\",\"clients\":" + String(WiFi.softAPgetStationNum()) + "}";
  stats += "}";
  server.send(200, "application/json", stats);
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
void sendRTCMToClients(uint8_t *data, uint16_t length) {
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
