
/* TODO 
  Hardware:
    SH1106 OLED display
    move to pcb

  Software:
    only update lcd text that changed
    -> cache previous values, only redraw what changed

    pad fields or add spaces - lcd.printf("H: %-5.1f%%", hum);

    async webserver

    add OTA updates  
    deep sleep / display timeout
    
    add uptime and wifi signal strength:
    WiFi.RSSI()
    ESP.getFreeHeap()
    millis()/1000

    split code into modules 

    free rtos

    19. Add captive portal / WiFi manager
    Instead of hardcoded WiFi:
      first boot creates AP
      configure WiFi from phone
      Use:
      WiFiManager
      This is a huge usability improvement
*/

#include <WiFi.h>
#include <Wire.h>
#include <ArduinoOTA.h>

#include <WebServer.h>

#include <LiquidCrystal_I2C.h>

#include <Adafruit_BME280.h>
#include <Adafruit_Sensor.h>

#include "time.h"

#include "secrets.h" // TODO put access data here

LiquidCrystal_I2C lcd(0x27, 20, 4);
Adafruit_BME280 bme;

float temp;
float hum;
float pressure;

IPAddress local_IP(192, 168, 2, 40);
IPAddress gateway(192, 168, 2, 1);
IPAddress subnet(255, 255, 255, 0); 
IPAddress primaryDNS(1, 1, 1, 1);

WebServer server(80);

bool display = 1;
unsigned long last_update = 0; 
const unsigned long update_interval = 1000;

const int DAY[3] = {5, 30, 0};
const int NIGHT[3] = {22, 0, 0};

bool dayMode = false;
const int MORNING_HOUR = 7;
const int NIGHT_HOUR   = 22;

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 3600;
const int   daylightOffset_sec = 3600;

struct tm timeinfo;

void initTime()
{
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  int retry = 0;
  const int maxRetries = 20;

  while (!getLocalTime(&timeinfo) && retry < maxRetries)
  {
    Serial.println("Waiting for time...");
    delay(500);
    retry++;
  }

  if (retry == maxRetries)
  {
    Serial.println("Failed to get time!");
  }
  else
  {
    Serial.println("Time synchronized!");
  }
}

void handleData()
{
  String json = "{";
  json += "\"temp\":" + String(temp, 1) + ",";
  json += "\"hum\":" + String(hum, 1) + ",";
  json += "\"pressure\":" + String(pressure, 1) + ",";
  json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
  json += "\"heap\":" + String(ESP.getFreeHeap()/1024) + ",";
  json += "\"uptime\":" + String(millis()/1000);
  json += "}";

  server.send(200, "application/json", json);
}

String htmlPage()
{
  String html = R"rawliteral(
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
      margin: 20px auto;
      padding: 20px;
      border-radius: 12px;
      width: 300px;
      box-shadow: 0 0 10px rgba(0,0,0,0.4);
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

    button:hover {
      background: #45a049;
    }
  </style>
</head>

<body>

  <h1>ESP32 Environment Monitor</h1>

  <div class="dashboard">
    <div class="card">
      <h2>Temperature</h2>
      <div class="value">
        <span id="temp">--</span> C
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
  </div>

  <div class="dashboard">
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
    document.getElementById('uptime').innerHTML = formatUptime(data.uptime);
  }
  catch(error)
  {
    console.log("Error fetching data:", error);
  }
}

async function toggleDisplay()
{
  try
  {
    const response = await fetch('/toggle');
    const status = await response.text();

    document.getElementById('status').innerHTML = status;
  }
  catch(error)
  {
    console.log("Toggle failed:", error);
  }
}

updateData(); // update immediately
setInterval(updateData, 1000); // then every second

</script>

</body>
</html>
)rawliteral";

  html.replace("%STATUS%", String(display ? "active" : "inactive"));

  return html;
}

void handleRoot() 
{
  server.send(200, "text/html", htmlPage());  
}

void handleToggle()
{
  display = !display;

  if (display)
    lcd.backlight();
  else
    lcd.noBacklight();

  server.send(200, "text/plain", display ? "active" : "inactive");
}

void setLightOnTime(int hour, int min, int sec, int flag)
{
  if (timeinfo.tm_hour == hour &&
    timeinfo.tm_min == min &&
    timeinfo.tm_sec == sec)
    if (!flag)
      lcd.noBacklight();
    else
    lcd.backlight();
}

void displayData()
{
  getLocalTime(&timeinfo);

  // Time strings
  char timeStr[9];
  char weekdayStr[12];
  char dateStr[16];
  strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);
  strftime(weekdayStr, sizeof(weekdayStr), "%A", &timeinfo);
  strftime(dateStr, sizeof(dateStr), "%d.%m.%Y", &timeinfo);

  // Read BME280
  temp = bme.readTemperature();
  hum  = bme.readHumidity();
  pressure = bme.readPressure() / 100.0F; // hPa

  // Line 0
  lcd.setCursor(4, 0);
  lcd.print(timeStr);
  lcd.print(" Uhr");
  // Line 1
  lcd.setCursor(0, 1);

  lcd.print(dateStr);
  lcd.print(" ");
  lcd.print(weekdayStr);
  
  // Line 2
  lcd.setCursor(0, 2);
  
  lcd.print("T: ");
  lcd.print(temp, 1);
  lcd.print((char)223); // ° symbol
  lcd.print("C  ");

  lcd.print("H: ");
  lcd.print(hum, 1);
  lcd.print("%");

  // Line 3: Pressure
  lcd.setCursor(0, 3);
  lcd.print("P: ");
  lcd.print(pressure, 1);
  lcd.print("hPa");
}

void setup()
{
  Serial.begin(115200);

  // I2C init (ESP32 default pins)
  Wire.begin(21, 22);

  // LCD init
  lcd.init();
  lcd.backlight();
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("Init BME280...");

  // Init BME280
  if (!bme.begin(0x76)) {   // try 0x77 if this fails
    lcd.setCursor(0, 1);
    lcd.print("BME not found!");
    while (1);
  }

  delay(1500);

  // WiFi connect
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Connecting WiFi");

  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS))
  {
    lcd.print("Wifi config failed");
    ESP.restart();
  }
  
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) 
  {
    delay(500);
    lcd.print(".");
  }

  lcd.clear();
  lcd.print("WiFi connected");

   // ---------------- OTA ----------------
  ArduinoOTA.setHostname("esp-device");
  ArduinoOTA.onStart([]() 
  {
    Serial.println("OTA Start");
  });

  ArduinoOTA.onEnd([]() 
  {
    Serial.println("\nOTA End");
  });

  ArduinoOTA.begin();

  // Time init
  initTime();
  delay(2000);
  lcd.clear();
  
  // Define URL handlers
  server.on("/", handleRoot);
  server.on("/toggle", handleToggle);
  server.on("/data", handleData);

  // Start the server
  server.begin();
  Serial.println("HTTP server started");
}

void loop()
{
  ArduinoOTA.handle();
  server.handleClient();
  
  if (millis() - last_update >= update_interval)
  {
    if (WiFi.status() != WL_CONNECTED)
      WiFi.reconnect();

    setLightOnTime(NIGHT[0], NIGHT[1], NIGHT[2], 0);
    setLightOnTime(DAY[0], DAY[1], DAY[2], 1);

    last_update = millis();
    displayData();
  }
}

