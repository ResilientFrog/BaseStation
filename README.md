# RTK Base Station

ESP32-based RTK (Real-Time Kinematic) GPS Base Station with web interface and logging capabilities.

## Features

- **RTK Base Station**: Survey-in or fixed position modes
- **Multi-GNSS Support**: GPS, GLONASS, Galileo, BeiDou
- **RTCM Streaming**: Send RTCM correction data to rovers via WiFi (port 2101)
- **Web Interface**: Configure base station and view logs at `http://192.168.4.1`
- **Real-time Logging**: Track system steps and sensor data
- **OLED Display**: View status on SSD1306 display

## Hardware

- **MCU**: ESP32
- **GNSS Module**: u-blox ZED-F9P (or compatible)
- **Display**: SSD1306 OLED (128x64, SPI)
- **Connection**: I2C for GNSS, SPI for display

## Pin Configuration

| Component | Pin |
|-----------|-----|
| OLED MOSI | GPIO 23 |
| OLED CLK  | GPIO 18 |
| OLED DC   | GPIO 16 |
| OLED CS   | GPIO 5 |
| OLED RST  | GPIO 17 |
| GNSS I2C  | Default SDA/SCL |

## Getting Started

### Prerequisites

- PlatformIO IDE
- ESP32 board
- u-blox GNSS module with RTK support

### Installation

1. Clone this repository
2. Copy `include/secrets.h.example` to `include/secrets.h` and configure WiFi credentials
3. Build and upload with PlatformIO

### WiFi Configuration

Create `include/secrets.h`:

```cpp
#define WIFI_AP_SSID "YourBaseStationName"
#define WIFI_AP_PASSWORD "YourPassword"
```

## Usage

### Base Station Modes

**Survey-In Mode**: Automatically determines position over time
- Set accuracy requirement (meters)
- Set minimum observation time (seconds)

**Fixed Mode**: Use known coordinates
- Enter latitude, longitude, altitude

### Web Interface

Connect to the base station WiFi AP and navigate to `http://192.168.4.1`

#### Endpoints

- `GET /` - Main configuration page with logs viewer
- `GET /logs` - All logs (JSON)
- `GET /logs/steps` - Step logs only (JSON)
- `GET /logs/data` - Sensor data logs (JSON)
- `GET /logs/stats` - Statistics (JSON)
- `POST /logs/clear` - Clear all logs

### RTK Rover Connection

Rovers can connect to port 2101 to receive RTCM correction data:

```cpp
WiFiClient rtkClient;
rtkClient.connect("192.168.4.1", 2101);

while (rtkClient.available()) {
  uint8_t rtcmByte = rtkClient.read();
  roverGNSS.pushRawData(rtcmByte);
}
```

## RTCM Messages

The base station generates:
- **RTCM 1005**: Base station coordinates (ECEF)
- **RTCM 1074**: GPS observations
- **RTCM 1084**: GLONASS observations
- **RTCM 1094**: Galileo observations
- **RTCM 1124**: BeiDou observations
- **RTCM 1230**: GLONASS code-phase biases

## Logging System

See [LOGGING_GUIDE.md](LOGGING_GUIDE.md) for detailed logging documentation.

## Project Structure

```
BaseStation/
├── include/          # Header files
├── lib/             # Libraries
├── src/
│   ├── main.cpp     # Main application
│   ├── baseRTK/     # RTK controller
│   ├── wi-fi/       # WiFi & web server
│   └── logger/      # Logging system
└── platformio.ini   # PlatformIO configuration
```

## License

This project is open source.

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.
