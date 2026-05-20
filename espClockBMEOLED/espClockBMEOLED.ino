
#include <WiFi.h>
#include <Wire.h>
#include <ArduinoOTA.h>
#include <WebServer.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

#include <Adafruit_BME280.h>
#include <Adafruit_Sensor.h>

#include "time.h"
#include "secrets.h"

// =====================================================
// DISPLAY
// =====================================================

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SH1106G oled(128, 64, &Wire, -1);

// =====================================================
// SENSOR
// =====================================================

Adafruit_BME280 bme;

float temp = 0;
float hum = 0;
float pressure = 0;

// =====================================================
// NETWORK
// =====================================================

IPAddress local_IP(192, 168, 2, 40);
IPAddress gateway(192, 168, 2, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(192, 168, 2, 2);

WebServer server(80);

// =====================================================
// SYSTEM
// =====================================================

bool displayEnabled = true;

unsigned long lastUpdate = 0;
const unsigned long updateInterval = 1000;

// =====================================================
// DAY / NIGHT
// =====================================================

const int DAY[3]   = {5, 30, 0};
const int NIGHT[3] = {22, 0, 0};

// =====================================================
// TIME
// =====================================================

const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600;
const int daylightOffset_sec = 3600;

struct tm timeinfo;

// =====================================================
// HTML PAGE
// =====================================================

String htmlPage()
{
  return R"rawliteral(
<!DOCTYPE html>
<html>

<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">

  <style>
    body {
      font-family: Arial;
      text-align: center;
      margin-top: 40px;
      background: #121212;
      color: white;
    }

    .dashboard {
      display: flex;
      flex-wrap: wrap;
      justify-content: center;
    }

    .card {
      background: #1E1E1E;
      margin: 20px;
      padding: 20px;
      border-radius: 12px;
      width: 280px;
    }

    .value {
      font-size: 2rem;
      font-weight: bold;
    }

    button {
      font-size: 20px;
      padding: 12px 20px;
      border: none;
      border-radius: 8px;
      background: #4CAF50;
      color: white;
      cursor: pointer;
    }
  </style>
</head>

<body>

<h1>ESP32 Environment Monitor</h1>

<div class="dashboard">

  <div class="card">
    <h2>Temperature</h2>
    <div class="value">
      <span id="temp">--</span> °C
    </div>
  </div>

  <div class="card">
    <h2>Humidity</h2>
    <div class="value">
      <span id="hum">--</span> %
    </div>
  </div>

  <div class="card">
    <h2>Pressure</h2>
    <div class="value">
      <span id="pressure">--</span> hPa
    </div>
  </div>

  <div class="card">
    <h2>WiFi Signal</h2>
    <div class="value">
      <span id="rssi">--</span> dBm
    </div>
  </div>

  <div class="card">
    <h2>Free Heap</h2>
    <div class="value">
      <span id="heap">--</span> KB
    </div>
  </div>

  <div class="card">
    <h2>Uptime</h2>
    <div class="value">
      <span id="uptime">--</span>
    </div>
  </div>

</div>

<button onclick="toggleDisplay()">Toggle Display</button>

<div id="status"></div>

<script>

function formatUptime(seconds)
{
  const h = Math.floor(seconds / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  const s = seconds % 60;

  return `${h}h ${m}m ${s}s`;
}

async function updateData()
{
  try
  {
    const response = await fetch('/data');
    const data = await response.json();

    document.getElementById('temp').innerHTML = data.temp;
    document.getElementById('hum').innerHTML = data.hum;
    document.getElementById('pressure').innerHTML = data.pressure;
    document.getElementById('rssi').innerHTML = data.rssi;
    document.getElementById('heap').innerHTML = data.heap;
    document.getElementById('uptime').innerHTML =
      formatUptime(data.uptime);
  }
  catch(error)
  {
    console.log(error);
  }
}

async function toggleDisplay()
{
  const response = await fetch('/toggle');
  const text = await response.text();

  document.getElementById('status').innerHTML = text;
}

updateData();
setInterval(updateData, 1000);

</script>

</body>
</html>
)rawliteral";
}

// =====================================================
// TIME
// =====================================================

void initTime()
{
  configTime(gmtOffset_sec,
             daylightOffset_sec,
             ntpServer);

  int retry = 0;

  while (!getLocalTime(&timeinfo) && retry < 20)
  {
    Serial.println("Waiting for NTP time...");
    delay(500);
    retry++;
  }

  if (retry >= 20)
    Serial.println("NTP failed");
  else
    Serial.println("Time synchronized");
}

// =====================================================
// WEB HANDLERS
// =====================================================

void handleRoot()
{
  server.send(200, "text/html", htmlPage());
}

void handleData()
{
  String json = "{";

  json += "\"temp\":" + String(temp, 1) + ",";
  json += "\"hum\":" + String(hum, 1) + ",";
  json += "\"pressure\":" + String(pressure, 1) + ",";
  json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
  json += "\"heap\":" + String(ESP.getFreeHeap() / 1024) + ",";
  json += "\"uptime\":" + String(millis() / 1000);

  json += "}";

  server.send(200, "application/json", json);
}

void handleToggle()
{
  displayEnabled = !displayEnabled;

  if (!displayEnabled)
  {
    oled.clearDisplay();
    oled.display();
  }

  server.send(200,
              "text/plain",
              displayEnabled ? "Display ON"
                             : "Display OFF");
}

// =====================================================
// OLED
// =====================================================

void drawDisplay()
{
  if (!displayEnabled)
    return;

  getLocalTime(&timeinfo);

  char timeStr[16];
  char dateStr[20];

  strftime(timeStr,
           sizeof(timeStr),
           "%H:%M:%S",
           &timeinfo);

  strftime(dateStr,
           sizeof(dateStr),
           "%d.%m.%Y",
           &timeinfo);

  temp = bme.readTemperature();
  hum = bme.readHumidity();
  pressure = bme.readPressure() / 100.0F;

  oled.clearDisplay();

  // TIME
  oled.setTextSize(2);
  oled.setCursor(0, 0);
  oled.println(timeStr);

  // DATE
  oled.setTextSize(1);
  oled.setCursor(0, 22);
  oled.println(dateStr);

  // TEMP
  oled.setCursor(0, 36);
  oled.printf("T: %.1f C", temp);

  // HUM
  oled.setCursor(0, 46);
  oled.printf("H: %.1f %%", hum);

  // PRESSURE
  oled.setCursor(0, 56);
  oled.printf("P: %.1f hPa", pressure);

  oled.display();
}

// =====================================================
// DISPLAY TIMER
// =====================================================

void setLightOnTime(int hour,
                    int min,
                    int sec,
                    bool enable)
{
  if (timeinfo.tm_hour == hour &&
      timeinfo.tm_min == min &&
      timeinfo.tm_sec == sec)
  {
    displayEnabled = enable;

    if (!enable)
    {
      oled.clearDisplay();
      oled.display();
    }
  }
}

// =====================================================
// SETUP
// =====================================================

void setup()
{
  Serial.begin(115200);

  Wire.begin(21, 22);

  // OLED
  if (!oled.begin(0x3C, true))
  {
    Serial.println("OLED init failed");
    while (1);
  }

  oled.clearDisplay();
  oled.setTextColor(SH110X_WHITE);
  oled.setTextSize(1);

  oled.setCursor(0, 0);
  oled.println("Booting...");
  oled.display();

  // BME280
  oled.println("Init BME280...");
  oled.display();

  if (!bme.begin(0x76))
  {
    oled.println("BME280 FAILED");
    oled.display();

    Serial.println("BME280 not found");
    while (1);
  }

  oled.println("BME280 OK");
  oled.display();

  delay(1000);

  // WIFI
  oled.clearDisplay();
  oled.setCursor(0, 0);
  oled.println("Connecting WiFi");
  oled.display();

  WiFi.mode(WIFI_STA);

  if (!WiFi.config(local_IP,
                   gateway,
                   subnet,
                   primaryDNS))
  {
    Serial.println("WiFi config failed");
  }

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);

    Serial.print(".");

    oled.print(".");
    oled.display();
  }

  Serial.println();
  Serial.println("WiFi connected");
  Serial.println(WiFi.localIP());

  oled.println();
  oled.println("WiFi connected");
  oled.display();

  delay(1000);

  // OTA
  ArduinoOTA.setHostname("esp32-monitor");

  ArduinoOTA.onStart([]()
  {
    Serial.println("OTA Start");
  });

  ArduinoOTA.onEnd([]()
  {
    Serial.println("OTA End");
  });

  ArduinoOTA.begin();

  // TIME
  initTime();

  // WEB SERVER
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/toggle", handleToggle);

  server.begin();

  Serial.println("HTTP server started");

  oled.clearDisplay();
  oled.setCursor(0, 0);
  oled.println("System Ready");
  oled.display();

  delay(1000);
}

// =====================================================
// LOOP
// =====================================================

void loop()
{
  ArduinoOTA.handle();
  server.handleClient();

  if (millis() - lastUpdate >= updateInterval)
  {
    lastUpdate = millis();

    if (WiFi.status() != WL_CONNECTED)
    {
      WiFi.reconnect();
    }

    getLocalTime(&timeinfo);

    setLightOnTime(
      NIGHT[0],
      NIGHT[1],
      NIGHT[2],
      false
    );

    setLightOnTime(
      DAY[0],
      DAY[1],
      DAY[2],
      true
    );

    drawDisplay();
  }
}
