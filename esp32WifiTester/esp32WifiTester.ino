
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
// #include <ArduinoJson.h>
#include "secrets.h" // TODO put access data here

/* EXAMPLE:
  const char* ap_ssid = "eps42";
  const char* ap_password = "wifi4esp32!";
*/

DNSServer dnsServer;

bool running = true; 

String sta_ssid = "";
String sta_password = "";

bool scanRunning = false;
int scanResultCount = 0;

unsigned long lastCheck = 0;
long rssi = 0;
int quality = 0;
int channel = 0;
bool wifi_status = false;
String ip_addr;

// connection time - track uptime since connect
// packet loss through peridoic gateway pings
// show competing networks and their rssi
// store last 50 samples and plit with chart.js
// captive portal pro level UX - DNSServer
// use async webserver

AsyncWebServer server(80);

String htmlPage() 
{
String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>WIFI TESTING</title>

<script src="https://cdn.jsdelivr.net/npm/chart.js"></script>

<script>
  let rssiData = [];
  let labels = [];
  let chart;

  // wait until page is loaded
  window.onload = () => {

  const ctx = document.getElementById('rssiChart').getContext('2d');

  chart = new Chart(ctx, {
    type: 'line',
    data: {
      labels: labels,
      datasets: [{
        label: 'RSSI (dBm)',
        data: rssiData,
        borderColor: 'blue',
        fill: false
      }]
    }
  });

  setInterval(updateData, 2000);
  };

  function updateData() {
  fetch('/data')
  .then(res => res.text())
  .then(text => {
    console.log("RAW:", text);
    return JSON.parse(text);
  })
    .then(data => {

      document.getElementById("rssi").innerText = data.rssi + " dBm";
      document.getElementById("wifi").innerText = data.status;
      document.getElementById("quality").innerText = data.quality;
      document.getElementById("channel").innerText = data.channel;
      document.getElementById("ip").innerText = data.ip;

      // graph
      if (rssiData.length > 50) {
        rssiData.shift();
        labels.shift();
      }

      rssiData.push(data.rssi);
      labels.push(new Date().toLocaleTimeString());

      chart.update();
    })
    .catch(err => console.error("FETCH ERROR:", err));
  }

  function scanWifi() {
  fetch('/scan')
    .then(res => res.json())
    .then(data => {
      if (data.status === "scanning") {
        setTimeout(scanWifi, 1000);
        return;
      }

      let select = document.getElementById("ssidSelect");
      select.innerHTML = "";

      data.forEach(net => {
        let opt = document.createElement("option");
        opt.value = net.ssid;
        opt.text = net.ssid + " (" + net.rssi + " dBm)";
        select.appendChild(opt);
      });
    });
  }

  function connectWifi() {
  let ssid = document.getElementById("ssidSelect").value;
  let password = document.getElementById("password").value;

  fetch('/connect', {
    method: 'POST',
    headers: {'Content-Type': 'application/x-www-form-urlencoded'},
    body: `ssid=${encodeURIComponent(ssid)}&password=${encodeURIComponent(password)}`
  });
  }
</script>

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
  </style>
</head>

<body>
  <div class="card">
    <h1>ESP32 WIFI TESTING SUITE</h1>
    <div class="status %STATUS_CLASS%">
      Status: %STATUS%
    </div>

    <button onclick="scanWifi()">Scan Networks</button><br><br>
      <select id="ssidSelect"></select><br><br>
      <input id="password" placeholder="Password" type="password"><br><br>
    <button onclick="connectWifi()">CONNECT</button>
  </div>

  <div>RSSI: <span id="rssi">%RSSI%</span></div>
  <div>Quality: <span id="quality">0</span></div>
  <div>Channel: <span id="channel">0</span></div>
  <div>IP: <span id="ip">0.0.0.0</span></div>
  <div>WIFI: <span id="wifi">%WIFI_STATUS%</span></div>

  <canvas id="rssiChart"></canvas>
</body>
</html>
)rawliteral";

  html.replace("%STATUS%", running ? "RUNNING" : "STOPPED");
  html.replace("%STATUS_CLASS%", running ? "running" : "stopped");

  html.replace("%RSSI%", String(rssi));
  html.replace("%WIFI_STATUS%", wifi_status ? "CONNECTED" : "DISCONNECTED");

  return html;
}

void setup() 
{
  Serial.begin(115200);
  delay(1000);

  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);

  WiFi.softAP(ap_ssid, ap_password);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  // dnsServer.start(53, "*", WiFi.softAPIP());
  WiFi.onEvent([](WiFiEvent_t event){
    Serial.println((int)event);
  switch(event) {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      Serial.println("STA Connected");
      break;

    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.print("Got IP: ");
      Serial.println(WiFi.localIP());
      break;

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.println("STA Disconnected");
      break;

    default:
      break;
  }
});

  // ROUTES HERE 
  server.onNotFound([](AsyncWebServerRequest *request)
  {
    request->redirect("/");
  });

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    request->send(200, "text/html", htmlPage());
  });

  server.on("/data", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    String json = "{";
    json += "\"rssi\":" + String(rssi) + ",";
    json += "\"status\":\"" + String(wifi_status ? "CONNECTED" : "DISCONNECTED") + "\",";
    json += "\"quality\":" + String(quality) + ",";
    json += "\"channel\":" + String(channel) + ",";
    json += "\"ip\":\"" + ip_addr + "\"";
    json += "}";
    request->send(200, "application/json", json);
  });

  server.on("/connect", HTTP_POST, [](AsyncWebServerRequest *request)
  {
    if (request->hasParam("ssid", true) && request->hasParam("password", true)) 
    {
      sta_ssid = request->getParam("ssid", true)->value();
      sta_password = request->getParam("password", true)->value();

      Serial.println("Connecting to: " + sta_ssid);

      WiFi.disconnect(true);   // 🔴 IMPORTANT
      delay(500);

      WiFi.begin(sta_ssid.c_str(), sta_password.c_str());
    }

    request->send(200, "text/html", "Connecting... <a href='/'>Back</a>");
  });

  server.on("/scan", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    if (!scanRunning) 
    {
      WiFi.scanNetworks(true); // start async
      scanRunning = true;
      request->send(200, "application/json", "{\"status\":\"scanning\"}");
      return;
    }

    int n = WiFi.scanComplete();

    if (n == WIFI_SCAN_RUNNING) 
    {
      request->send(200, "application/json", "{\"status\":\"scanning\"}");
      return;
    }

    // scan finished
    String json = "[";

    for (int i = 0; i < n; i++) 
    {
      if (i) json += ",";
      json += "{";
      json += "\"ssid\":\"" + WiFi.SSID(i) + "\",";
      json += "\"rssi\":" + String(WiFi.RSSI(i));
      json += "}";
    }

    json += "]";

    WiFi.scanDelete();
    scanRunning = false;

    request->send(200, "application/json", json);
  });

  server.begin(); // AFTER routes
}

void loop() 
{
  // dnsServer.processNextRequest();

  if (millis() - lastCheck < 2000)
    return;

  Serial.println("WIFI STATUS:");
  Serial.println(WiFi.status());

  lastCheck = millis();

  wifi_status = (WiFi.status() == WL_CONNECTED);

  if (wifi_status) 
  {
    ip_addr = WiFi.localIP().toString();
    rssi = WiFi.RSSI();
    channel = WiFi.channel();
    quality = constrain(2 * (rssi + 100), 0, 100);
  } 
  else 
  {
    ip_addr = "0.0.0.0";
    rssi = 0;
    channel = 0;
    quality = 0;
  }
}
