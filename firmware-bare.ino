
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <TinyGPSPlus.h>

// ============================================================
// WIFI CONFIGURATION
// ============================================================

const char* WIFI_SSID = "pldt";
const char* WIFI_PASSWORD = "pass";

// ============================================================
// MQTT / HIVEMQ CLOUD
// ============================================================

const char* MQTT_HOST = "hehehe.eu.hivemq.cloud";
const int MQTT_PORT = 8883;

const char* MQTT_USERNAME = "username";
const char* MQTT_PASSWORD = "pass";

// Tracker identity
const char* TRACKER_ID = "TRACK-001";

// MQTT topic
String locationTopic;

// ============================================================
// GPS CONFIGURATION
// ============================================================

#define GPS_RX_PIN 16
#define GPS_TX_PIN 17
#define GPS_BAUD 9600

#define GPS_REPORT_INTERVAL 5000

// ============================================================
// OBJECTS
// ============================================================

HardwareSerial GPSSerial(1);

TinyGPSPlus gps;

WiFiClientSecure secureClient;
PubSubClient mqttClient(secureClient);

// ============================================================
// STATE
// ============================================================

unsigned long lastGpsReport = 0;
unsigned long lastWiFiAttempt = 0;
unsigned long lastMQTTAttempt = 0;

const unsigned long RECONNECT_INTERVAL = 5000;

// ============================================================
// GPS READER
// ============================================================

void readGPS() {

    while (GPSSerial.available()) {

        char c = GPSSerial.read();

        gps.encode(c);
    }
}

// ============================================================
// WIFI CONNECTION
// ============================================================

void connectWiFi() {

    if (WiFi.status() == WL_CONNECTED) {
        return;
    }

    Serial.println("[WiFi] Connecting...");

    WiFi.begin(
        WIFI_SSID,
        WIFI_PASSWORD
    );

    unsigned long start = millis();

    while (
        WiFi.status() != WL_CONNECTED &&
        millis() - start < 10000
    ) {

        delay(250);
        Serial.print(".");
    }

    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {

        Serial.println("[WiFi] Connected");

        Serial.print("[WiFi] IP: ");
        Serial.println(WiFi.localIP());

    } else {

        Serial.println("[WiFi] Connection failed");
    }
}

// ============================================================
// MQTT CONNECTION
// ============================================================

void connectMQTT() {

    if (mqttClient.connected()) {
        return;
    }

    if (WiFi.status() != WL_CONNECTED) {
        return;
    }

    Serial.println("[MQTT] Connecting...");

    String clientId =
        "esp32-" +
        String(TRACKER_ID) +
        "-" +
        String((uint32_t)ESP.getEfuseMac(), HEX);

    if (
        mqttClient.connect(
            clientId.c_str(),
            MQTT_USERNAME,
            MQTT_PASSWORD
        )
    ) {

        Serial.println("[MQTT] Connected");

    } else {

        Serial.print("[MQTT] Failed. State: ");
        Serial.println(mqttClient.state());
    }
}

// ============================================================
// GPS STATUS
// ============================================================

String getGpsStatus() {

    if (!gps.location.isValid()) {
        return "NO FIX";
    }

    if (
        gps.hdop.isValid() &&
        gps.hdop.hdop() <= 2.5
    ) {
        return "GOOD FIX";
    }

    return "POOR FIX";
}

// ============================================================
// PUBLISH GPS LOCATION
// ============================================================

void publishLocation() {

    if (!mqttClient.connected()) {
        return;
    }

    // Require a recent GPS position
    if (
        !gps.location.isValid() ||
        gps.location.age() > 3000
    ) {

        Serial.println("[GPS] No recent valid fix");
        return;
    }

    double latitude = gps.location.lat();
    double longitude = gps.location.lng();

    int satellites = gps.satellites.isValid()
        ? gps.satellites.value()
        : 0;

    double hdop = gps.hdop.isValid()
        ? gps.hdop.hdop()
        : 99.99;

    String gpsStatus = getGpsStatus();

    // Build JSON payload
    char payload[256];

    snprintf(
        payload,
        sizeof(payload),
        "{"
            "\"lat\":%.6f,"
            "\"lng\":%.6f,"
            "\"gps_status\":\"%s\","
            "\"satellites\":%d,"
            "\"hdop\":%.2f"
        "}",
        latitude,
        longitude,
        gpsStatus.c_str(),
        satellites,
        hdop
    );

    // Publish retained location
    bool success = mqttClient.publish(
        locationTopic.c_str(),
        payload,
        true
    );

    if (success) {

        Serial.println("[MQTT] Location published");

        Serial.print("[MQTT] Topic: ");
        Serial.println(locationTopic);

        Serial.print("[MQTT] Payload: ");
        Serial.println(payload);

    } else {

        Serial.println("[MQTT] Publish failed");
    }
}

// ============================================================
// SETUP
// ============================================================

void setup() {

    Serial.begin(115200);

    Serial.println();
    Serial.println("=================================");
    Serial.println("BIYAHE GPS TRACKER");
    Serial.println("Tracker: " + String(TRACKER_ID));
    Serial.println("=================================");

    // GPS UART
    GPSSerial.begin(
        GPS_BAUD,
        SERIAL_8N1,
        GPS_RX_PIN,
        GPS_TX_PIN
    );

    Serial.println("[GPS] UART initialized");

    // MQTT topic
    locationTopic =
        "biyahe/tracker/" +
        String(TRACKER_ID) +
        "/location";

    // TLS configuration
    // DEVELOPMENT ONLY: disables certificate verification.
    secureClient.setInsecure();

    // MQTT configuration
    mqttClient.setServer(
        MQTT_HOST,
        MQTT_PORT
    );

    mqttClient.setBufferSize(512);

    // WiFi
    connectWiFi();

    // MQTT
    connectMQTT();
}

// ============================================================
// MAIN LOOP
// ============================================================

void loop() {

    // Always read GPS first
    readGPS();

    unsigned long now = millis();

    // Maintain WiFi connection
    if (WiFi.status() != WL_CONNECTED) {

        if (
            now - lastWiFiAttempt >=
            RECONNECT_INTERVAL
        ) {

            lastWiFiAttempt = now;
            connectWiFi();
        }

        return;
    }

    // Maintain MQTT connection
    if (!mqttClient.connected()) {

        if (
            now - lastMQTTAttempt >=
            RECONNECT_INTERVAL
        ) {

            lastMQTTAttempt = now;
            connectMQTT();
        }

        return;
    }

    mqttClient.loop();

    // Periodic GPS reporting
    if (
        now - lastGpsReport >=
        GPS_REPORT_INTERVAL
    ) {

        lastGpsReport = now;

        publishLocation();
    }
}
