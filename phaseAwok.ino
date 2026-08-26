/*
  IoT Soil Management System
  PHASE A - Sensor validation / Wokwi test

  Reads:
  - DHT22 temperature
  - DHT22 humidity
  - Capacitive soil moisture analogue output

  Then prints the readings to Serial Monitor.
*/

#include <DHT.h>

// -------------------- PIN SETUP --------------------
const int DHT_PIN  = 21;
const int SOIL_PIN = 34;

DHT dht(DHT_PIN, DHT22);

// -------------------- SOIL CALIBRATION --------------------
// Replace these after testing your real sensor.
// Dry soil usually gives a different raw value from wet soil.
const int SOIL_DRY = 3000;
const int SOIL_WET = 1300;

// Convert the raw ADC reading into an easy 0-100% value.
int readSoilPercent()
{
  int rawValue = analogRead(SOIL_PIN);

  int percent = map(rawValue, SOIL_DRY, SOIL_WET, 0, 100);
  percent = constrain(percent, 0, 100);

  return percent;
}

void setup()
{
  Serial.begin(115200);
  dht.begin();

  Serial.println("Phase A - Sensor validation");
}

void loop()
{
  // Read the DHT22.
  float temperature = dht.readTemperature();
  float humidity    = dht.readHumidity();

  // Read the soil sensor.
  int soilRaw     = analogRead(SOIL_PIN);
  int soilPercent = readSoilPercent();

  // DHT22 returns NaN if a reading fails.
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
    Serial.print(" % | Soil raw: ");
    Serial.print(soilRaw);
    Serial.print(" | Soil moisture: ");
    Serial.print(soilPercent);
    Serial.println(" %");
  }

  // DHT22 should not be read too rapidly.
  delay(2000);
}
