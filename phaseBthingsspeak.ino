/*
  IoT Soil Management System
  PART 1 / PHASE B - ThingSpeak cloud monitoring

  Requirements covered:
  - Read temperature, humidity and soil moisture
  - Connect ESP32 to Wi-Fi
  - Upload all three values to ThingSpeak
  - Upload every 30 seconds
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <DHT.h>

// -------------------- PIN SETUP --------------------
const int DHT_PIN  = 21;
const int SOIL_PIN = 34;

DHT dht(DHT_PIN, DHT22);

// -------------------- WIFI --------------------
// Wokwi:
//   SSID = "Wokwi-GUEST"
//   password = ""
//
// Physical ESP32:
//   Replace these with your Wi-Fi / hotspot details.
const char* WIFI_SSID     = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";

// -------------------- THINGSPEAK --------------------
const char* THINGSPEAK_URL = "http://api.thingspeak.com/update";

// Replace with your ThingSpeak WRITE API key.
const char* THINGSPEAK_WRITE_KEY = "YOUR_WRITE_API_KEY";

// -------------------- SOIL CALIBRATION --------------------
// Replace these after testing your real sensor.
const int SOIL_DRY = 3000;
const int SOIL_WET = 1300;

int readSoilPercent()
{
  int rawValue = analogRead(SOIL_PIN);

  int percent = map(rawValue, SOIL_DRY, SOIL_WET, 0, 100);
  percent = constrain(percent, 0, 100);

  return percent;
}

void connectWiFi()
{
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to Wi-Fi");

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("Wi-Fi connected.");
  Serial.print("ESP32 IP address: ");
  Serial.println(WiFi.localIP());
}

void setup()
{
  Serial.begin(115200);
  dht.begin();

  connectWiFi();
}

void loop()
{
  // -------------------- READ SENSORS --------------------
  float temperature = dht.readTemperature();
  float humidity    = dht.readHumidity();
  int soilPercent   = readSoilPercent();

  if (isnan(temperature) || isnan(humidity))
  {
    Serial.println("ERROR: DHT22 reading failed.");
  }
  else
  {
    Serial.print("Temperature: ");
    Serial.print(temperature, 1);
    Serial.print(" C | Humidity: ");
    Serial.print(humidity, 1);
    Serial.print(" % | Soil moisture: ");
    Serial.print(soilPercent);
    Serial.println(" %");

    // -------------------- SEND TO THINGSPEAK --------------------
    if (WiFi.status() == WL_CONNECTED)
    {
      HTTPClient http;

      // ThingSpeak field assignment:
      // field1 = temperature
      // field2 = humidity
      // field3 = soil moisture
      String url = String(THINGSPEAK_URL)
                 + "?api_key=" + THINGSPEAK_WRITE_KEY
                 + "&field1=" + String(temperature, 1)
                 + "&field2=" + String(humidity, 1)
                 + "&field3=" + String(soilPercent);

      http.begin(url);

      int responseCode = http.GET();

      Serial.print("ThingSpeak HTTP response: ");
      Serial.println(responseCode);

      http.end();
    }
    else
    {
      Serial.println("Wi-Fi disconnected. Reconnecting...");
      connectWiFi();
    }
  }

  // Assessment requirement: upload every 30 seconds.
  delay(30000);
}
