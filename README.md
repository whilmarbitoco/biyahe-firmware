# ESP32 GPS Tracker

ESP32 firmware for the Biyahe GPS tracking system.

The tracker uses MQTT over TLS to send GPS location updates and receive SMS commands from the backend. A GPS module provides the vehicle location, while a GSM module handles outgoing SMS messages.

## Features

- GPS location tracking
- MQTT over TLS
- Periodic location reporting
- GSM SMS sending
- SMS delivery acknowledgement
- Automatic Wi-Fi and MQTT reconnection
- GPS fix status reporting

## Hardware

- ESP32
- GPS module such as NEO-6M or NEO-M8N
- GSM module such as SIM800L/SIM900
- Wi-Fi connection

### Default Pins

| Module | ESP32 |
|---|---|
| GPS RX | GPIO 16 |
| GPS TX | GPIO 17 |
| GSM RX | GPIO 26 |
| GSM TX | GPIO 27 |

## Libraries

Install these Arduino libraries:

- PubSubClient
- ArduinoJson
- TinyGPSPlus

## Configuration

Update the configuration values in the firmware:

```cpp
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

const char* MQTT_HOST     = "YOUR_CLUSTER.s1.eu.hivemq.cloud";
const int   MQTT_PORT     = 8883;

const char* MQTT_USERNAME  = "YOUR_MQTT_USERNAME";
const char* MQTT_PASSWORD  = "YOUR_MQTT_PASSWORD";

const char* TRACKER_ID = "TRACK-001";
```

Each tracker must have a unique `TRACKER_ID` registered in the backend.

## MQTT Topics

For `TRACK-001`:

### Location

```text
biyahe/tracker/TRACK-001/location
```

Example:

```json
{
  "lat": 14.5833,
  "lng": 120.9667,
  "gps_status": "GOOD FIX"
}
```

### SMS Commands

The device subscribes to:

```text
biyahe/tracker/TRACK-001/command
```

Example:

```json
{
  "command": "send_sms",
  "phone": "09171234567",
  "message": "Your booking is confirmed!",
  "sms_id": 42
}
```

### SMS Acknowledgement

After successfully sending an SMS, the device publishes to:

```text
biyahe/tracker/TRACK-001/sms_sent
```

Example:

```json
{
  "sms_id": 42
}
```

## Location Reporting

The ESP32 attempts to publish the current GPS location every 5 seconds.

GPS status can be:

- `GOOD FIX`
- `POOR FIX`
- `NO FIX`

## Security

MQTT uses TLS on port `8883`.

The current development firmware uses:

```cpp
secureClient.setInsecure();
```

This disables certificate verification and should **not** be used in production. Production firmware should use the HiveMQ CA certificate with `setCACert()`.

## Data Flow

```text
GPS ──→ ESP32 ──MQTT/TLS──→ HiveMQ
                              │
                              ▼
                           Backend
                              │
                              ▼
                            Redis
                              │
                              ▼
                         PostgreSQL
```

For SMS:

```text
Backend ──MQTT──→ ESP32 ──→ GSM ──→ Recipient
                       │
                       └──→ sms_sent ──→ Backend
```

## Development

1. Install the required Arduino libraries.
2. Configure Wi-Fi, MQTT credentials, and `TRACKER_ID`.
3. Connect the GPS and GSM modules to the configured UART pins.
4. Upload the firmware to the ESP32.
5. Open the Serial Monitor at `115200` baud.
6. Verify Wi-Fi and MQTT connection.
7. Verify GPS fix and location reports.
