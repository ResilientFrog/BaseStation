# BaseStation Logging System Documentation

## Overview

A comprehensive logging system has been added to track system steps and sensor data. All logs are accessible via HTTP endpoints from the WiFi AP and stored locally on the ESP32's LittleFS filesystem.

## Features

### 1. **Step Logging**
- Tracks system events and operations
- Log levels: INFO, DEBUG, WARN, ERROR
- Components: System, WiFi, RTK, Display
- Timestamps in milliseconds
- Stored in memory (last 100 entries) and on LittleFS

### 2. **Data Logging**
- Records sensor measurements
- Captures: latitude, longitude, altitude, fix type, satellite count
- Data types: RTK_STATUS, RTCM messages
- Stored in memory (last 100 entries) and on LittleFS

### 3. **HTTP Endpoints**
All endpoints are accessible on port 80 of the WiFi AP:

#### Get All Logs (Combined)
```
GET /logs
```
Returns JSON with both step and data logs.

#### Get Step Logs Only
```
GET /logs/steps
```
Returns array of step log entries:
```json
[
  {
    "timestamp": 1234567,
    "level": "INFO",
    "component": "RTK",
    "message": "GNSS initialized successfully"
  }
]
```

#### Get Data Logs Only
```
GET /logs/data
```
Returns array of data measurements:
```json
[
  {
    "timestamp": 1234567,
    "dataType": "RTK_STATUS",
    "latitude": 45.1234567,
    "longitude": -75.1234567,
    "altitude": 150.50,
    "fixType": 5,
    "satellites": 12
  }
]
```

#### Get Statistics
```
GET /logs/stats
```
Returns logging statistics:
```json
{
  "logs": {
    "stepLogsCount": 42,
    "dataLogsCount": 15
  },
  "wifi": {
    "ip": "192.168.4.1",
    "clients": 1
  }
}
```

#### Clear All Logs
```
POST /logs/clear
```
Clears all in-memory and file-based logs.

## Data Locations

### In-Memory Storage
- Last 100 step logs
- Last 100 data logs
- Cleared on reboot

### File Storage (LittleFS)
- Step logs: `/logs/steps.txt`
- Data logs: `/logs/data.txt`
- Format: CSV for easy parsing

## Logging API

### Using the Logger in Your Code

```cpp
#include "logger/Logger.h"

// Log an informational step
logger.logInfo("Component", "Message");

// Log a debug message
logger.logDebug("Component", "Debug info");

// Log a warning
logger.logWarn("Component", "Warning message");

// Log an error
logger.logError("Component", "Error message");

// Log sensor data
logger.logData(
  "RTK_STATUS",        // dataType
  45.1234567,          // latitude
  -75.1234567,         // longitude
  150.50,              // altitude
  5,                   // fixType (0=No Fix, 5=RTK FIXED)
  12                   // satellites
);

// Log RTCM messages
logger.logRTCMMessage(1005, count);
```

## Example Usage

### Getting Logs via Terminal/Browser

```bash
# Get all logs
curl http://192.168.4.1/logs

# Get only step logs
curl http://192.168.4.1/logs/steps

# Get sensor data
curl http://192.168.4.1/logs/data

# Get statistics
curl http://192.168.4.1/logs/stats

# Clear logs (POST request)
curl -X POST http://192.168.4.1/logs/clear
```

### Parsing JSON Response Example

```python
import requests
import json

# Fetch logs
response = requests.get('http://192.168.4.1/logs')
logs = response.json()

# Access steps
for step in logs['steps']:
    print(f"[{step['level']}] {step['component']}: {step['message']}")

# Access data
for data in logs['data']:
    print(f"Position: {data['latitude']}, {data['longitude']} @ {data['altitude']}m")
```

## Integration Points

The logging system has been integrated into:

1. **main.cpp**
   - System initialization
   - Setup sequence logging

2. **baseRTKController.cpp**
   - RTK status checks
   - Mode configuration
   - RTCM processing
   - Survey-In operations

3. **wiFiConnectionController.cpp**
   - WiFi AP initialization
   - Web server startup
   - Configuration updates
   - Rover connections

## Performance Notes

- Minimal memory footprint: Vector-based storage
- CSV file format for easy analysis
- Automatic file rolling (overwrites oldest entries)
- Non-blocking logging operations
- Safe file access with LittleFS

## Troubleshooting

### Logs Not Appearing
1. Check LittleFS is mounted: `LittleFS.begin()` in setup
2. Verify `/logs` directory exists
3. Check serial output for logger initialization messages

### Logs Not Accessible via HTTP
1. Ensure WiFi AP is running
2. Connect to AP SSID and access `http://192.168.4.1`
3. Verify web server has logs endpoints registered

### Memory Full
- In-memory buffers automatically roll at 100 entries
- File-based logs persist and can be cleared via `/logs/clear` endpoint

## Future Enhancements

Possible additions:
- Log rotation by time/size
- Remote log transmission
- Log filtering by level/component
- Binary log format for compression
- Real-time WebSocket streaming
