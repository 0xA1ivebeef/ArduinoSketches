
#include <WiFi.h>
#include <WebServer.h>
#include "secrets.h"

IPAddress local_IP(192, 168, 2, 11);
IPAddress gateway(192, 168, 2, 1);
IPAddress subnet(255, 255, 255, 0);

long rssi;
bool wifi_status = false;

int iter = 0;
bool cycle_running = false;
const int max_iter = 4;

#define RELAY1 23
#define RELAY2 5
#define RELAY3 4
#define PUMP_PIN 13

const int relayPins[] = {RELAY1, RELAY2, RELAY3};
const int relayCount = 3;

const int valveDelay = 7000;
const int zoneTime[] = {120, 120, 90};

// ===== STATE =====
WebServer server(80);

bool running = false;
unsigned long relayStartTime = 0;

String htmlPage() 
{
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Irrigation</title>
  <style>
    body {
      font-family: Arial, sans-serif;
      background: #f4f7f9;
      text-align: center;
      margin: 0;
      padding: 0;
    }
    .card {
      background: white;
      max-width: 400px;
      margin: 60px auto;
      padding: 20px;
      border-radius: 12px;
      box-shadow: 0 4px 12px rgba(0,0,0,0.1);
    }
    h1 {
      margin-bottom: 20px;
      color: #333;
    }
    .status {
      font-size: 20px;
      margin-bottom: 20px;
      font-weight: bold;
    }
    .running {
      color: green;
    }
    .stopped {
      color: red;
    }
    button {
      width: 120px;
      padding: 12px;
      margin: 10px;
      font-size: 16px;
      border: none;
      border-radius: 8px;
      cursor: pointer;
    }
    .start {
      background-color: #28a745;
      color: white;
    }
    .stop {
      background-color: #dc3545;
      color: white;
    }
    button:hover {
      opacity: 0.9;
    }

    .iteration {
      font-size: 18px;
      margin-bottom: 15px;
      padding: 10px;
      border-radius: 8px;
      background: #e9f5ff;
      color: #007bff;
      font-weight: bold;
    }
  </style>
</head>
<body>

<div class="card">
  <h1>ESP32 Irrigation</h1>
  <div class="status %STATUS_CLASS%">
    Status: %STATUS%
  </div>

  <form action="/start" method="POST">
    <button class="start">START</button>
  </form>

  <form action="/stop" method="POST">
    <button class="stop">STOP</button>
  </form>
</div>

<div class="iteration">
  Iteration: %ITER% / %MAX_ITER%
</div>

<div class="rssi">
  RSSI: %RSSI% dBm
</div>

<div class="wifi status">
  WIFI: %WIFI_STATUS%
</div>

</body>
</html>
)rawliteral";

  html.replace("%STATUS%", running ? "RUNNING" : "STOPPED");
  html.replace("%STATUS_CLASS%", running ? "running" : "stopped");

  html.replace("%ITER%", String(iter+1));
  html.replace("%MAX_ITER%", String(max_iter));

  html.replace("%RSSI%", String(rssi));
  html.replace("%WIFI_STATUS%", wifi_status ? "CONNECTED" : "DISCONNECTED");

  return html;
}

// ===== AUTH CHECK =====
bool isAuthenticated() 
{
  if (!server.authenticate(www_username, www_password)) 
  {
    server.requestAuthentication();
    return false;
  }
  return true;
}

void turn_off()
{
  digitalWrite(PUMP_PIN, HIGH);
  delay(valveDelay);
  for (int i = 0; i < relayCount; i++) 
    digitalWrite(relayPins[i], HIGH);
}

// ===== ROUTES =====
void handleRoot() 
{
  if (!isAuthenticated()) return;
  server.send(200, "text/html", htmlPage());
}

void handleStart() 
{
  if (!isAuthenticated()) return;

  running = true;
  iter = 0;

  server.send(200, "text/html", htmlPage());
}

void handleStop() 
{
  if (!isAuthenticated()) return;

  running = false;
  cycle_running = false;
  iter = 0;
  turn_off();

  server.send(200, "text/html", htmlPage());
}


void check_wifi()
{
  Serial.println("checking wifi");
  wifi_status = (WiFi.status() == WL_CONNECTED) ? true : false;
  if (!wifi_status) 
  {
    Serial.println("WiFi lost, attempting to reconnect...");
    WiFi.disconnect();  // Disconnect the current session, if any
    WiFi.begin(ssid, password);  // Reattempt connection
  }
  Serial.println("wifi connected");
}

void run_cycle()
{   
  Serial.println("run_cycle()");

  int currentRelay = 0;
  digitalWrite(PUMP_PIN, LOW);
  relayStartTime = millis();
  
  while (cycle_running)
  {
    // Serial.println("cycle running");
   
    // Check if Wi-Fi is connected
    // rssi = WiFi.RSSI();
    server.handleClient();

    if (!running)
    {
      Serial.println("not running, shutting off");
      turn_off();
      cycle_running = false;
      return;
    }

    // cycle finished
    if (currentRelay >= relayCount) 
    {
      Serial.println("cycle finished");
      cycle_running = false;
      return;
    }

    // current relay on
    Serial.println("turning on current relay");
    digitalWrite(relayPins[currentRelay], LOW);

    unsigned long elapsed = (millis() - relayStartTime) / 1000;
    if (elapsed >= zoneTime[currentRelay]) 
    {
      Serial.println("timeout, next relay on");

      // next relay on before current off 
      if (currentRelay < relayCount - 1)
        digitalWrite(relayPins[currentRelay+1], LOW);
      else
      {
        Serial.println("finished last relay in this cycle, turning off");
        turn_off(); // finished last relay in this cycle
      }

      // current relay off 
      Serial.println("doing next relay");
      delay(valveDelay);
      digitalWrite(relayPins[currentRelay], HIGH);

      currentRelay++;
      relayStartTime = millis();
    }
  }
}

// ===== SETUP =====
void setup() 
{
  Serial.begin(115200);
  delay(1000);

  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  
  // Set static IP BEFORE connecting
  if (!WiFi.config(local_IP, gateway, subnet)) 
    Serial.println("STA Failed to configure");
  
  WiFi.begin(ssid, password);
  rssi = WiFi.RSSI();
  WiFi.softAP(ap_ssid, ap_password);
 
  Serial.println("Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) 
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nConnected to STA network");
  Serial.print("STA IP: ");
  Serial.println(WiFi.localIP());

  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  pinMode(PUMP_PIN, OUTPUT);
  digitalWrite(PUMP_PIN, HIGH);

  // init pins
  for (int i = 0; i < relayCount; i++) 
  {
    pinMode(relayPins[i], OUTPUT);
    digitalWrite(relayPins[i], HIGH);
  }

  server.on("/", handleRoot);
  server.on("/start", HTTP_POST, handleStart);
  server.on("/stop", HTTP_POST, handleStop);

  server.begin();
}

void loop() 
{
  // check_wifi();
  // rssi = WiFi.RSSI();
  server.handleClient();

  if (!running) 
    return;

  if (!cycle_running)
  {
    Serial.println("no cycle running");
    if (iter < max_iter)
    {
      check_wifi(); // checking once per cycle for wifi

      Serial.println("currently in iteration");
      Serial.println(iter+1);
      Serial.println("starting new cycle");
      
      cycle_running = true;
      run_cycle();
      iter++;
    }
    else
    {
      Serial.println("all cycles finished");
      running = false;
    }
  }
  // delay(500);
}

