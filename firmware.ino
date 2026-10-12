#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <TinyGPSPlus.h>

// ============================================================
// CONFIGURATION
// ============================================================

// WiFi
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// MQTT / HiveMQ Cloud
const char* MQTT_HOST     = "YOUR_CLUSTER.s1.eu.hivemq.cloud";
const int   MQTT_PORT     = 8883;

const char* MQTT_USERNAME  = "YOUR_MQTT_USERNAME";
const char* MQTT_PASSWORD  = "YOUR_MQTT_PASSWORD";

// Tracker identity
const char* TRACKER_ID = "TRACK-001";

// GPS report interval
const unsigned long GPS_REPORT_INTERVAL = 5000;

// ============================================================
// UART PINS
// ============================================================

// GPS
#define GPS_RX_PIN 16
#define GPS_TX_PIN 17

// GSM
#define GSM_RX_PIN 26
#define GSM_TX_PIN 27

#define GPS_BAUD 9600
#define GSM_BAUD 9600

// ============================================================
// OBJECTS
// ============================================================

HardwareSerial GPSSerial(1);
HardwareSerial GSMSerial(2);

TinyGPSPlus gps;

WiFiClientSecure secureClient;
PubSubClient mqttClient(secureClient);

// ============================================================
// MQTT TOPICS
// ============================================================

String locationTopic;
String smsSentTopic;
String commandTopic;

// ============================================================
// STATE
// ============================================================

unsigned long lastGpsReport = 0;

bool gsmReady = false;

// ============================================================
// FUNCTION DECLARATIONS
// ============================================================

void connectWiFi();
void connectMQTT();

void mqttCallback(char* topic, byte* payload, unsigned int length);

void processCommand(const char* payload);

bool sendSMS(const String& phone, const String& message);

void publishLocation();

void publishSmsSent(int smsId);

String getGpsStatus();

void readGPS();

bool waitForGSMResponse(
    const String& expected,
    unsigned long timeout
);

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

    // --------------------------------------------------------
    // UART INITIALIZATION
    // --------------------------------------------------------

    GPSSerial.begin(
        GPS_BAUD,
        SERIAL_8N1,
        GPS_RX_PIN,
        GPS_TX_PIN
    );

    GSMSerial.begin(
        GSM_BAUD,
        SERIAL_8N1,
        GSM_RX_PIN,
        GSM_TX_PIN
    );

    // --------------------------------------------------------
    // MQTT TOPICS
    // --------------------------------------------------------

    locationTopic =
        "biyahe/tracker/" +
        String(TRACKER_ID) +
        "/location";

    smsSentTopic =
        "biyahe/tracker/" +
        String(TRACKER_ID) +
        "/sms_sent";

    commandTopic =
        "biyahe/tracker/" +
        String(TRACKER_ID) +
        "/command";

    // --------------------------------------------------------
    // GSM INITIALIZATION
    // --------------------------------------------------------

    delay(1000);

    GSMSerial.println("AT");
    delay(500);

    GSMSerial.println("ATE0");
    delay(500);

    GSMSerial.println("AT+CMGF=1");
    delay(500);

    gsmReady = true;

    Serial.println("[GSM] Initialized");

    // --------------------------------------------------------
    // WIFI
    // --------------------------------------------------------

    connectWiFi();

    // --------------------------------------------------------
    // MQTT TLS
    // --------------------------------------------------------

    /*
     * For production, replace this with the HiveMQ CA
     * certificate instead of using setInsecure().
     *
     * Example:
     *
     * secureClient.setCACert(HIVEMQ_CA_CERT);
     *
     * setInsecure() disables certificate verification and
     * should only be used during development/testing.
     */

    secureClient.setInsecure();

    mqttClient.setServer(
        MQTT_HOST,
        MQTT_PORT
    );

    mqttClient.setCallback(mqttCallback);

    mqttClient.setBufferSize(1024);

    connectMQTT();
}

// ============================================================
// MAIN LOOP
// ============================================================

void loop() {

    // --------------------------------------------------------
    // Maintain WiFi
    // --------------------------------------------------------

    if (WiFi.status() != WL_CONNECTED) {
        connectWiFi();
    }

    // --------------------------------------------------------
    // Maintain MQTT
    // --------------------------------------------------------

    if (!mqttClient.connected()) {
        connectMQTT();
    }

    mqttClient.loop();

    // --------------------------------------------------------
    // Read GPS continuously
    // --------------------------------------------------------

    readGPS();

    // --------------------------------------------------------
    // Periodic GPS report
    // --------------------------------------------------------

    unsigned long now = millis();

    if (now - lastGpsReport >= GPS_REPORT_INTERVAL) {

        lastGpsReport = now;

        publishLocation();
    }
}

// ============================================================
// WIFI
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
        millis() - start < 15000
    ) {

        delay(500);

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

    /*
     * Client ID should be unique.
     */

    String clientId =
        "esp32-" +
        String(TRACKER_ID) +
        "-" +
        String((uint32_t)ESP.getEfuseMac(), HEX);

    while (!mqttClient.connected()) {

        Serial.print("[MQTT] Client ID: ");
        Serial.println(clientId);

        bool connected = mqttClient.connect(
            clientId.c_str(),
            MQTT_USERNAME,
            MQTT_PASSWORD
        );

        if (connected) {

            Serial.println("[MQTT] Connected");

            /*
             * Subscribe to commands:
             *
             * biyahe/tracker/TRACK-001/command
             */

            if (
                mqttClient.subscribe(
                    commandTopic.c_str(),
                    1
                )
            ) {

                Serial.println(
                    "[MQTT] Subscribed: " +
                    commandTopic
                );

            } else {

                Serial.println(
                    "[MQTT] Subscription failed"
                );
            }

            /*
             * Optional online message.
             */

            return;

        } else {

            Serial.print(
                "[MQTT] Failed, state="
            );

            Serial.println(
                mqttClient.state()
            );

            delay(3000);
        }
    }
}

// ============================================================
// MQTT CALLBACK
// ============================================================

void mqttCallback(
    char* topic,
    byte* payload,
    unsigned int length
) {

    Serial.println();
    Serial.println("[MQTT] Message received");

    Serial.print("[MQTT] Topic: ");
    Serial.println(topic);

    // --------------------------------------------------------
    // Convert payload to String
    // --------------------------------------------------------

    String message;

    for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i];
    }

    Serial.print("[MQTT] Payload: ");
    Serial.println(message);

    // --------------------------------------------------------
    // Verify topic
    // --------------------------------------------------------

    if (String(topic) == commandTopic) {

        processCommand(
            message.c_str()
        );
    }
}

// ============================================================
// PROCESS MQTT COMMAND
// ============================================================

void processCommand(const char* payload) {

    StaticJsonDocument<512> doc;

    DeserializationError error =
        deserializeJson(doc, payload);

    if (error) {

        Serial.print(
            "[MQTT] Invalid JSON: "
        );

        Serial.println(
            error.c_str()
        );

        return;
    }

    const char* command =
        doc["command"];

    if (!command) {

        Serial.println(
            "[MQTT] Missing command"
        );

        return;
    }

    // ========================================================
    // SEND SMS
    // ========================================================

    if (
        String(command) ==
        "send_sms"
    ) {

        const char* phone =
            doc["phone"];

        const char* message =
            doc["message"];

        int smsId =
            doc["sms_id"] | 0;

        if (
            !phone ||
            !message ||
            smsId <= 0
        ) {

            Serial.println(
                "[MQTT] Invalid SMS command"
            );

            return;
        }

        Serial.println(
            "[GSM] Sending SMS..."
        );

        bool success =
            sendSMS(
                String(phone),
                String(message)
            );

        if (success) {

            Serial.println(
                "[GSM] SMS sent successfully"
            );

            /*
             * Notify backend.
             *
             * {
             *   "sms_id": 42
             * }
             */

            publishSmsSent(smsId);

        } else {

            Serial.println(
                "[GSM] SMS failed"
            );
        }

        return;
    }

    Serial.print(
        "[MQTT] Unknown command: "
    );

    Serial.println(command);
}

// ============================================================
// SEND SMS
// ============================================================

bool sendSMS(
    const String& phone,
    const String& message
) {

    if (!gsmReady) {

        Serial.println(
            "[GSM] Not ready"
        );

        return false;
    }

    Serial.println(
        "[GSM] Recipient: " +
        phone
    );

    Serial.println(
        "[GSM] Message: " +
        message
    );

    // --------------------------------------------------------
    // Text mode
    // --------------------------------------------------------

    GSMSerial.println(
        "AT+CMGF=1"
    );

    if (
        !waitForGSMResponse(
            "OK",
            3000
        )
    ) {

        Serial.println(
            "[GSM] Failed to enter text mode"
        );

        return false;
    }

    // --------------------------------------------------------
    // SMS command
    // --------------------------------------------------------

    GSMSerial.print(
        "AT+CMGS=\""
    );

    GSMSerial.print(phone);

    GSMSerial.println("\"");

    // Wait for >
    if (
        !waitForGSMResponse(
            ">",
            5000
        )
    ) {

        Serial.println(
            "[GSM] No SMS prompt"
        );

        return false;
    }

    // --------------------------------------------------------
    // Message
    // --------------------------------------------------------

    GSMSerial.print(message);

    // CTRL+Z
    GSMSerial.write(26);

    // --------------------------------------------------------
    // Wait for confirmation
    // --------------------------------------------------------

    if (
        waitForGSMResponse(
            "OK",
            15000
        )
    ) {

        return true;
    }

    return false;
}

// ============================================================
// GSM RESPONSE
// ============================================================

bool waitForGSMResponse(
    const String& expected,
    unsigned long timeout
) {

    unsigned long start =
        millis();

    String response;

    while (
        millis() - start <
        timeout
    ) {

        while (
            GSMSerial.available()
        ) {

            char c =
                GSMSerial.read();

            response += c;

            Serial.write(c);

            if (
                response.indexOf(
                    expected
                ) >= 0
            ) {

                return true;
            }
        }

        delay(10);
    }

    return false;
}

// ============================================================
// GPS READER
// ============================================================

void readGPS() {

    while (
        GPSSerial.available()
    ) {

        char c =
            GPSSerial.read();

        gps.encode(c);
    }
}

// ============================================================
// GPS STATUS
// ============================================================

String getGpsStatus() {

    if (!gps.location.isValid()) {

        return "NO FIX";
    }

    /*
     * TinyGPS++ gives us HDOP and satellite
     * information which can be used as a
     * simple quality indicator.
     */

    if (
        gps.hdop.isValid() &&
        gps.hdop.hdop() <= 2.5
    ) {

        return "GOOD FIX";
    }

    return "POOR FIX";
}

// ============================================================
// PUBLISH LOCATION
// ============================================================

void publishLocation() {

    if (!mqttClient.connected()) {
        return;
    }

    StaticJsonDocument<256> doc;

    // --------------------------------------------------------
    // No GPS fix
    // --------------------------------------------------------

    if (!gps.location.isValid()) {

        doc["lat"] = 0;
        doc["lng"] = 0;
        doc["gps_status"] = "NO FIX";

    } else {

        doc["lat"] =
            gps.location.lat();

        doc["lng"] =
            gps.location.lng();

        doc["gps_status"] =
            getGpsStatus();
    }

    char buffer[256];

    size_t length =
        serializeJson(
            doc,
            buffer
        );

    bool success =
        mqttClient.publish(
            locationTopic.c_str(),
            buffer,
            length,
            true
        );

    if (success) {

        Serial.println(
            "[MQTT] Location published"
        );

        Serial.print(
            "[MQTT] "
        );

        Serial.println(buffer);

    } else {

        Serial.println(
            "[MQTT] Location publish failed"
        );
    }
}

// ============================================================
// SMS ACK
// ============================================================

void publishSmsSent(int smsId) {

    if (!mqttClient.connected()) {
        return;
    }

    StaticJsonDocument<128> doc;

    doc["sms_id"] = smsId;

    char buffer[128];

    size_t length =
        serializeJson(
            doc,
            buffer
        );

    bool success =
        mqttClient.publish(
            smsSentTopic.c_str(),
            buffer,
            length,
            true
        );

    if (success) {

        Serial.println(
            "[MQTT] SMS acknowledgement sent"
        );

        Serial.print(
            "[MQTT] "
        );

        Serial.println(buffer);

    } else {

        Serial.println(
            "[MQTT] SMS acknowledgement failed"
        );
    }
}
