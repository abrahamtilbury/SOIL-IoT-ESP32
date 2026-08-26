/*
  IoT Soil Management System
  PART 2 / PHASE D - MQTT + local automatic watering

  Requirements covered:
  - DHT22 temperature and humidity
  - Capacitive soil moisture sensor
  - MQTT communication through Mosquitto
  - Publish sensor data for Node-RED
  - Subscribe to Node-RED rain forecast
  - Automatic pump control from soil moisture + rain forecast
  - Manual MQTT pump override: ON / OFF / AUTO
  - Interrupt-driven emergency stop with highest priority
  - L298N motor driver controls the 5 V pump

  L298N assumption:
  - Common L298N module
  - ENA jumper fitted, so only IN1 and IN2 are required for ON/OFF control
*/

#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>

// ============================================================
// PIN SETUP
// ============================================================

const int DHT_PIN   = 21;
const int SOIL_PIN  = 34;
const int ESTOP_PIN = 18;

const int PUMP_IN1  = 26;
const int PUMP_IN2  = 27;

DHT dht(DHT_PIN, DHT22);

// ============================================================
// WIFI
// ============================================================

// For the physical ESP32, replace these with the Wi-Fi network
// used by both the ESP32 and the computer running Mosquitto.
const char* WIFI_SSID     = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// ============================================================
// MQTT / MOSQUITTO
// ============================================================

// IMPORTANT:
// This must be the LAN IP address of the computer running Mosquitto.
// Do NOT use "localhost" because localhost on the ESP32 means the ESP32.
const char* MQTT_BROKER_IP = "192.168.1.100";
const int   MQTT_PORT      = 1883;

// ESP32 -> Node-RED
const char* TOPIC_SENSORS     = "soilmonitor/sensors";
const char* TOPIC_PUMP_STATUS = "soilmonitor/status/pump";

// Node-RED -> ESP32
const char* TOPIC_WEATHER     = "weather/forecast/rain";
const char* TOPIC_PUMP_CMD    = "command/pump/on";

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// ============================================================
// SOIL MOISTURE
// ============================================================

// Replace with values measured from your real sensor.
const int SOIL_DRY = 3000;
const int SOIL_WET = 1300;

// The assessment lets you choose the threshold.
// Below 35% = soil is considered dry.
const int SOIL_THRESHOLD = 35;

// ============================================================
// TIMING
// ============================================================

const unsigned long SENSOR_INTERVAL_MS = 5000;

unsigned long lastSensorTime = 0;
unsigned long lastWiFiAttempt = 0;
unsigned long lastMQTTAttempt = 0;

// ============================================================
// SYSTEM STATE
// ============================================================

int  soilPercent = 0;
bool sensorDataValid = false;

// Weather state received from Node-RED.
bool weatherReceived = false;
bool rainPredicted   = false;

// Manual control:
// false = automatic mode
// true  = manual mode
bool manualOverride = false;
bool manualPumpOn   = false;

// Actual pump state.
bool pumpOn = false;

// Emergency stop is written by an interrupt.
// "volatile" tells the compiler this value can change unexpectedly.
volatile bool emergencyStopLatched = false;

bool emergencyMessagePrinted = false;

// ============================================================
// EMERGENCY STOP INTERRUPT
// ============================================================

// Keep interrupt code VERY short.
// Pressing the button connects GPIO18 to GND.
// INPUT_PULLUP means:
//   normal  = HIGH
//   pressed = LOW
//
// FALLING therefore means the button has just been pressed.
void IRAM_ATTR emergencyStopISR()
{
  emergencyStopLatched = true;
}

// ============================================================
// SOIL SENSOR FUNCTION
// ============================================================

int readSoilPercent()
{
  int rawValue = analogRead(SOIL_PIN);

  int percent = map(rawValue, SOIL_DRY, SOIL_WET, 0, 100);
  percent = constrain(percent, 0, 100);

  return percent;
}

// ============================================================
// PUMP FUNCTION
// ============================================================

void setPump(bool requestedOn)
{
  // Emergency stop ALWAYS has highest priority.
  if (emergencyStopLatched)
  {
    requestedOn = false;
  }

  // No change needed.
  if (requestedOn == pumpOn)
  {
    return;
  }

  if (requestedOn)
  {
    // One direction is enough for the water pump.
    digitalWrite(PUMP_IN1, HIGH);
    digitalWrite(PUMP_IN2, LOW);

    pumpOn = true;
    Serial.println("PUMP: ON");
  }
  else
  {
    // Both LOW = motor stopped.
    digitalWrite(PUMP_IN1, LOW);
    digitalWrite(PUMP_IN2, LOW);

    pumpOn = false;
    Serial.println("PUMP: OFF");
  }

  // Report the actual pump state to Node-RED.
  if (mqttClient.connected())
  {
    mqttClient.publish(
      TOPIC_PUMP_STATUS,
      pumpOn ? "ON" : "OFF",
      true                 // retained message
    );
  }
}

// ============================================================
// CONTROL LOGIC
// ============================================================

void updatePumpControl()
{
  // ----------------------------------------------------------
  // PRIORITY 1: EMERGENCY STOP
  // ----------------------------------------------------------
  if (emergencyStopLatched)
  {
    setPump(false);
    return;
  }

  // ----------------------------------------------------------
  // PRIORITY 2: MANUAL MQTT OVERRIDE
  // ----------------------------------------------------------
  if (manualOverride)
  {
    setPump(manualPumpOn);
    return;
  }

  // ----------------------------------------------------------
  // PRIORITY 3: AUTOMATIC CONTROL
  // ----------------------------------------------------------

  // Fail safe:
  // do not automatically water unless both sensor data
  // and a weather forecast are available.
  if (!sensorDataValid || !weatherReceived)
  {
    setPump(false);
    return;
  }

  // Assessment logic:
  // dry soil + NO predicted rain = pump ON
  // everything else             = pump OFF
  if (soilPercent < SOIL_THRESHOLD && !rainPredicted)
  {
    setPump(true);
  }
  else
  {
    setPump(false);
  }
}

// ============================================================
// MQTT CALLBACK
// ============================================================

// PubSubClient calls this function whenever a subscribed MQTT
// message arrives.
void mqttCallback(char* topic, byte* payload, unsigned int length)
{
  String message;

  for (unsigned int i = 0; i < length; i++)
  {
    message += (char)payload[i];
  }

  message.trim();

  Serial.print("MQTT RX [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(message);

  // ----------------------------------------------------------
  // WEATHER FORECAST FROM NODE-RED
  // ----------------------------------------------------------
  if (String(topic) == TOPIC_WEATHER)
  {
    message.toLowerCase();

    if (message == "rain_predicted")
    {
      rainPredicted = true;
      weatherReceived = true;
    }
    else if (message == "no_rain")
    {
      rainPredicted = false;
      weatherReceived = true;
    }
    else
    {
      Serial.println("Unknown weather message.");
      return;
    }

    updatePumpControl();
  }

  // ----------------------------------------------------------
  // MANUAL PUMP COMMAND FROM NODE-RED
  // ----------------------------------------------------------
  else if (String(topic) == TOPIC_PUMP_CMD)
  {
    message.toUpperCase();

    if (message == "ON")
    {
      manualOverride = true;
      manualPumpOn = true;
    }
    else if (message == "OFF")
    {
      manualOverride = true;
      manualPumpOn = false;
    }
    else if (message == "AUTO")
    {
      manualOverride = false;
    }
    else
    {
      Serial.println("Unknown pump command.");
      return;
    }

    updatePumpControl();
  }
}

// ============================================================
// WIFI CONNECTION
// ============================================================

void startWiFi()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to Wi-Fi");

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(250);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("Wi-Fi connected.");
  Serial.print("ESP32 IP address: ");
  Serial.println(WiFi.localIP());
}

// Non-blocking reconnect after startup.
void maintainWiFi()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    return;
  }

  // Safety: if communications are down, stop the automatic pump.
  setPump(false);

  if (millis() - lastWiFiAttempt >= 5000)
  {
    lastWiFiAttempt = millis();

    Serial.println("Attempting Wi-Fi reconnect...");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
}

// ============================================================
// MQTT CONNECTION
// ============================================================

void maintainMQTT()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    return;
  }

  if (mqttClient.connected())
  {
    return;
  }

  // Safety: no broker means we may have stale weather data.
  setPump(false);

  // Try once every 5 seconds without freezing the main loop.
  if (millis() - lastMQTTAttempt < 5000)
  {
    return;
  }

  lastMQTTAttempt = millis();

  Serial.print("Connecting to Mosquitto...");

  if (mqttClient.connect("ESP32-Soil-System"))
  {
    Serial.println("connected.");

    // Subscribe to data coming FROM Node-RED.
    mqttClient.subscribe(TOPIC_WEATHER);
    mqttClient.subscribe(TOPIC_PUMP_CMD);

    Serial.println("MQTT subscriptions active.");
  }
  else
  {
    Serial.print("failed. MQTT state = ");
    Serial.println(mqttClient.state());
  }
}

// ============================================================
// SENSOR READ + MQTT PUBLISH
// ============================================================

void readAndPublishSensors()
{
  float temperature = dht.readTemperature();
  float humidity    = dht.readHumidity();

  soilPercent = readSoilPercent();

  if (isnan(temperature) || isnan(humidity))
  {
    sensorDataValid = false;
    Serial.println("ERROR: DHT22 reading failed.");

    updatePumpControl();
    return;
  }

  sensorDataValid = true;

  Serial.print("Temperature: ");
  Serial.print(temperature, 1);
  Serial.print(" C | Humidity: ");
  Serial.print(humidity, 1);
  Serial.print(" % | Soil moisture: ");
  Serial.print(soilPercent);
  Serial.println(" %");

  // JSON is convenient for Node-RED's JSON node.
  char payload[120];

  snprintf(
    payload,
    sizeof(payload),
    "{\"temperature\":%.1f,\"humidity\":%.1f,\"soil\":%d}",
    temperature,
    humidity,
    soilPercent
  );

  if (mqttClient.connected())
  {
    mqttClient.publish(TOPIC_SENSORS, payload);

    Serial.print("MQTT TX: ");
    Serial.println(payload);
  }

  // A fresh soil reading may change the automatic pump decision.
  updatePumpControl();
}

// ============================================================
// SETUP
// ============================================================

void setup()
{
  Serial.begin(115200);

  // Start the DHT22.
  dht.begin();

  // Pump outputs.
  pinMode(PUMP_IN1, OUTPUT);
  pinMode(PUMP_IN2, OUTPUT);

  // ALWAYS start with the pump OFF.
  digitalWrite(PUMP_IN1, LOW);
  digitalWrite(PUMP_IN2, LOW);
  pumpOn = false;

  // Emergency stop input.
  pinMode(ESTOP_PIN, INPUT_PULLUP);

  // Interrupt fires immediately when the button is pressed.
  attachInterrupt(
    digitalPinToInterrupt(ESTOP_PIN),
    emergencyStopISR,
    FALLING
  );

  // Connect to Wi-Fi.
  startWiFi();

  // Configure MQTT.
  mqttClient.setServer(MQTT_BROKER_IP, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);

  Serial.println("IoT Soil Management System ready.");
}

// ============================================================
// MAIN LOOP
// ============================================================

void loop()
{
  // ----------------------------------------------------------
  // HIGHEST PRIORITY: EMERGENCY STOP
  // ----------------------------------------------------------
  if (emergencyStopLatched)
  {
    setPump(false);

    if (!emergencyMessagePrinted)
    {
      Serial.println();
      Serial.println("*** EMERGENCY STOP LATCHED ***");
      Serial.println("Pump disabled until ESP32 reset / power cycle.");
      emergencyMessagePrinted = true;
    }

    // Deliberately remain latched until reset.
    // The interrupt itself has already stopped normal operation.
    return;
  }

  // Keep network services alive.
  maintainWiFi();
  maintainMQTT();

  // PubSubClient needs loop() called frequently to receive messages.
  if (mqttClient.connected())
  {
    mqttClient.loop();
  }

  // Non-blocking sensor timer.
  if (millis() - lastSensorTime >= SENSOR_INTERVAL_MS)
  {
    lastSensorTime = millis();
    readAndPublishSensors();
  }
}
