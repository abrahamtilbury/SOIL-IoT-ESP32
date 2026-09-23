/*
  IoT Soil Management System
  PHASE B - ThingSpeak cloud monitoring

  - Reads temperature, humidity and soil moisture
  - Connect ESP32 to Wi-Fi
  - Upload all three values to ThingSpeak
  - Upload every 30 seconds
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <DHT.h>

// -------------------- PIN SETUP --------------------
const int DHT_PIN  = 4;
const int SOIL_PIN = 34;

DHT dht(DHT_PIN, DHT22);

// -------------------- WIFI --------------------
const char* WIFI_SSID     = "Abe's iPhone";
const char* WIFI_PASSWORD = "nintendo";

// -------------------- THINGSPEAK --------------------
const char* THINGSPEAK_URL = "http://api.thingspeak.com/update";

// Replace with your ThingSpeak WRITE API key.
const char* THINGSPEAK_WRITE_KEY = "KC3SZFBU0P52006L";

// -------------------- SOIL CALIBRATION --------------------
const int SOIL_DRY = 0;
const int SOIL_WET = 1267;

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

  // upload every 30 seconds.
  delay(30000);
}
