#include "HttpServer.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <esp_system.h>

#include "../config/Config.h"
#include "../logger/Logger.h"

namespace {

const char* wifiModeName(WifiModeState mode) {
  switch (mode) {
    case WifiModeState::Connecting:
      return "connecting";
    case WifiModeState::Station:
      return "station";
    case WifiModeState::AccessPoint:
      return "ap";
    case WifiModeState::Failed:
      return "failed";
    default:
      return "off";
  }
}

const char* usbStateName(Mc331State state) {
  switch (state) {
    case Mc331State::Waiting:
      return "waiting";
    case Mc331State::Connected:
      return "connected";
    case Mc331State::Applying:
      return "applying";
    case Mc331State::Fixed:
      return "fixed";
    case Mc331State::Error:
      return "error";
    case Mc331State::Disconnected:
      return "disconnected";
    default:
      return "idle";
  }
}

const char kFallbackHtml[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="es"><head>
<meta charset="UTF-8"/><meta name="viewport" content="width=device-width,initial-scale=1"/>
<title>FosiFix Setup</title>
<style>
body{font-family:system-ui,sans-serif;margin:0;background:#f3efe6;color:#18202b}
main{max-width:420px;margin:2rem auto;padding:1.2rem;background:#fffdf8;border-radius:16px;box-shadow:0 12px 40px rgba(0,0,0,.08)}
h1{margin:0 0 .3rem;font-size:1.4rem}p{color:#5d6875;line-height:1.4}
label{display:block;margin:.8rem 0 .3rem;font-size:.9rem;color:#5d6875}
input,button,select{width:100%;padding:.75rem;border-radius:10px;border:1px solid #d7d0c4;font:inherit;box-sizing:border-box}
button{margin-top:1rem;background:#0f766e;color:#fff;border:0;font-weight:600}
#nets{list-style:none;padding:0;margin:.6rem 0;max-height:220px;overflow:auto}
#nets li{padding:.65rem .75rem;border:1px solid #e6dfd2;border-radius:10px;margin:.35rem 0;cursor:pointer}
#nets li:hover{border-color:#0f766e}
.msg{margin-top:1rem;padding:.8rem;border-radius:10px;background:#e7f5f2;display:none}
</style></head><body><main>
<h1>FosiFix</h1>
<p>Elegí tu WiFi de casa. Después volvé a tu red y abrí <b>http://fosifix.local</b></p>
<button type="button" id="scan">Buscar redes</button>
<ul id="nets"></ul>
<label>SSID</label><input id="ssid"/>
<label>Password</label><input id="pass" type="password"/>
<label>Hostname</label><input id="host" value="fosifix"/>
<button type="button" id="save">Guardar y conectar</button>
<div class="msg" id="msg"></div>
<script>
const nets=document.getElementById('nets');
const msg=document.getElementById('msg');
async function scan(){
  nets.innerHTML='Buscando…';
  const r=await fetch('/api/wifi/scan'); const list=await r.json();
  nets.innerHTML='';
  list.sort((a,b)=>b.rssi-a.rssi).forEach(n=>{
    const li=document.createElement('li');
    li.textContent=(n.ssid||'(oculta)')+'  '+n.rssi+' dBm'+(n.secure?' 🔒':'');
    li.onclick=()=>{document.getElementById('ssid').value=n.ssid||''};
    nets.appendChild(li);
  });
}
document.getElementById('scan').onclick=scan;
document.getElementById('save').onclick=async()=>{
  const body={ssid:ssid.value,password:pass.value,hostname:host.value||'fosifix',autoMode:true,retryIntervalMs:3000,reboot:true};
  const r=await fetch('/api/settings',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
  const d=await r.json();
  msg.style.display='block';
  msg.textContent=d.ok?'Guardado. Volvé a tu WiFi de casa y abrí http://fosifix.local':'Error al guardar';
};
scan().catch(()=>{});
</script></main></body></html>)HTML";

}  // namespace

HttpServer::HttpServer(Settings& settings, WifiManager& wifi, Mc331Host& usb)
    : settings_(settings),
      wifi_(wifi),
      usb_(usb),
      server_(Config::kHttpPort),
      ws_(Config::kWsPort) {}

bool HttpServer::begin() {
  if (!LittleFS.begin(true)) {
    Logger::instance().error("LittleFS mount failed");
    return false;
  }

  bootMs_ = millis();
  setupRoutes();
  server_.begin();
  ws_.begin();
  ws_.onEvent([this](uint8_t num, WStype_t type, uint8_t* payload,
                     size_t length) {
    (void)payload;
    (void)length;
    if (type == WStype_CONNECTED) {
      ws_.sendTXT(num, "{\"type\":\"hello\",\"app\":\"FosiFix\"}");
      String statusMsg =
          String("{\"type\":\"status\",\"data\":") + buildStatusJson() + "}";
      ws_.sendTXT(num, statusMsg);
    }
  });

  if (wifi_.isSetupMode()) {
    dns_.start(53, "*", WiFi.softAPIP());
    dnsActive_ = true;
    Logger::instance().info("Captive DNS activo");
  }

  started_ = true;
  Logger::instance().info("Servidor iniciado");
  return true;
}

void HttpServer::setupRoutes() {
  server_.on("/", HTTP_GET, [this]() { handleRoot(); });
  server_.on("/index.html", HTTP_GET, [this]() { handleRoot(); });
  server_.on("/style.css", HTTP_GET, [this]() { serveFile("/style.css"); });
  server_.on("/app.js", HTTP_GET, [this]() { serveFile("/app.js"); });

  server_.on("/api/status", HTTP_GET, [this]() { handleStatus(); });
  server_.on("/api/logs", HTTP_GET, [this]() { handleLogs(); });
  server_.on("/api/fix", HTTP_POST, [this]() { handleFix(); });
  server_.on("/api/reboot", HTTP_POST, [this]() { handleReboot(); });
  server_.on("/api/settings", HTTP_GET, [this]() { handleSettingsGet(); });
  server_.on("/api/settings", HTTP_POST, [this]() { handleSettingsPost(); });
  server_.on("/api/wifi/scan", HTTP_GET, [this]() { handleWifiScan(); });
  server_.on(
      "/api/ota", HTTP_POST, [this]() { handleOta(); },
      [this]() {
        HTTPUpload& upload = server_.upload();
        if (upload.status == UPLOAD_FILE_START) {
          Logger::instance().info("OTA start: " + upload.filename);
          if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            Update.printError(Serial);
          }
        } else if (upload.status == UPLOAD_FILE_WRITE) {
          if (Update.write(upload.buf, upload.currentSize) !=
              upload.currentSize) {
            Update.printError(Serial);
          }
        } else if (upload.status == UPLOAD_FILE_END) {
          if (Update.end(true)) {
            Logger::instance().info("OTA success");
          } else {
            Update.printError(Serial);
            Logger::instance().error("OTA failed");
          }
        }
      });

  server_.on("/generate_204", HTTP_GET, [this]() { handleRoot(); });
  server_.on("/hotspot-detect.html", HTTP_GET, [this]() { handleRoot(); });
  server_.on("/canonical.html", HTTP_GET, [this]() { handleRoot(); });
  server_.on("/ncsi.txt", HTTP_GET, [this]() { handleRoot(); });
  server_.on("/connecttest.txt", HTTP_GET, [this]() { handleRoot(); });
  server_.onNotFound([this]() { handleNotFound(); });
}

void HttpServer::loop() {
  if (!started_) {
    return;
  }
  if (dnsActive_) {
    dns_.processNextRequest();
  }
  server_.handleClient();
  ws_.loop();

  const uint32_t now = millis();
  if (now - lastStatusBroadcastMs_ >= Config::kStatusBroadcastIntervalMs) {
    lastStatusBroadcastMs_ = now;
    broadcastStatus();
  }
}

void HttpServer::sendFallbackUi() {
  server_.sendHeader("Cache-Control", "no-store");
  server_.send_P(200, "text/html", kFallbackHtml);
}

void HttpServer::handleRoot() {
  if (!serveFile("/index.html")) {
    sendFallbackUi();
  }
}

void HttpServer::handleStatus() {
  server_.send(200, "application/json", buildStatusJson());
}

void HttpServer::handleLogs() {
  server_.send(200, "application/json", Logger::instance().toJsonArray());
}

void HttpServer::handleWifiScan() {
  server_.send(200, "application/json", wifi_.scanNetworksJson());
}

void HttpServer::handleFix() {
  JsonDocument doc;
  const bool accepted = usb_.requestFix();
  doc["ok"] = accepted;
  doc["message"] = accepted ? "fix queued" : "fix rejected";
  String out;
  serializeJson(doc, out);
  server_.send(accepted ? 200 : 500, "application/json", out);
}

void HttpServer::handleReboot() {
  JsonDocument doc;
  doc["ok"] = true;
  doc["message"] = "rebooting";
  String out;
  serializeJson(doc, out);
  server_.send(200, "application/json", out);
  delay(300);
  ESP.restart();
}

void HttpServer::handleSettingsGet() {
  JsonDocument doc;
  const auto& s = settings_.data();
  doc["ssid"] = s.ssid;
  doc["hostname"] = s.hostname;
  doc["autoMode"] = s.autoMode;
  doc["retryIntervalMs"] = s.retryIntervalMs;
  doc["periodicFixMs"] = s.periodicFixMs;
  doc["configured"] = s.configured;
  doc["hasPassword"] = s.password.length() > 0;
  doc["setupMode"] = wifi_.isSetupMode();
  String out;
  serializeJson(doc, out);
  server_.send(200, "application/json", out);
}

void HttpServer::handleSettingsPost() {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, server_.arg("plain"));
  if (err) {
    server_.send(400, "application/json", "{\"ok\":false,\"error\":\"json\"}");
    return;
  }

  auto& s = settings_.data();
  if (doc["ssid"].is<const char*>()) {
    s.ssid = doc["ssid"].as<const char*>();
  }
  if (doc["password"].is<const char*>()) {
    s.password = doc["password"].as<const char*>();
  }
  if (doc["hostname"].is<const char*>()) {
    s.hostname = doc["hostname"].as<const char*>();
  }
  if (doc["autoMode"].is<bool>()) {
    s.autoMode = doc["autoMode"].as<bool>();
    usb_.setAutoMode(s.autoMode);
  }
  if (doc["retryIntervalMs"].is<uint32_t>()) {
    s.retryIntervalMs = doc["retryIntervalMs"].as<uint32_t>();
    usb_.setRetryIntervalMs(s.retryIntervalMs);
  }
  if (doc["periodicFixMs"].is<uint32_t>()) {
    s.periodicFixMs = doc["periodicFixMs"].as<uint32_t>();
    usb_.setPeriodicFixMs(s.periodicFixMs);
  }

  if (s.ssid.length() > 0) {
    s.configured = true;
  }

  const bool saved = settings_.save();
  JsonDocument resp;
  resp["ok"] = saved;
  resp["reboot"] = doc["reboot"] | true;
  resp["next"] = "Reconnect to your home WiFi and open http://" + s.hostname +
                 ".local";
  String out;
  serializeJson(resp, out);
  server_.send(saved ? 200 : 500, "application/json", out);

  if (saved && (doc["reboot"] | true)) {
    delay(800);
    ESP.restart();
  }
}

void HttpServer::handleOta() {
  JsonDocument doc;
  doc["ok"] = !Update.hasError();
  doc["message"] = Update.hasError() ? "ota failed" : "ota ok";
  String out;
  serializeJson(doc, out);
  server_.send(Update.hasError() ? 500 : 200, "application/json", out);
  if (!Update.hasError()) {
    delay(500);
    ESP.restart();
  }
}

void HttpServer::handleNotFound() {
  if (wifi_.isSetupMode() || wifi_.isAccessPoint()) {
    handleRoot();
    return;
  }
  server_.send(404, "application/json", "{\"error\":\"not found\"}");
}

String HttpServer::buildStatusJson() const {
  JsonDocument doc;
  const auto wifi = wifi_.status();
  const auto usb = usb_.status();

  doc["app"] = Config::kAppName;
  doc["version"] = Config::kVersion;
  doc["uptimeMs"] = millis() - bootMs_;
  doc["heapFree"] = ESP.getFreeHeap();
  doc["heapSize"] = ESP.getHeapSize();
  doc["psramFree"] = ESP.getFreePsram();
  doc["psramSize"] = ESP.getPsramSize();
  doc["flashSize"] = ESP.getFlashChipSize();

  JsonObject wifiObj = doc["wifi"].to<JsonObject>();
  wifiObj["mode"] = wifiModeName(wifi.mode);
  wifiObj["setupMode"] = wifi.setupMode;
  wifiObj["connected"] = wifi.connected;
  wifiObj["ssid"] = wifi.ssid;
  wifiObj["ip"] = wifi.ip;
  wifiObj["hostname"] = wifi.hostname;
  wifiObj["rssi"] = wifi.rssi;

  JsonObject usbObj = doc["usb"].to<JsonObject>();
  usbObj["state"] = usbStateName(usb.state);
  usbObj["connected"] = usb.connected;
  usbObj["fixApplied"] = usb.fixApplied;
  usbObj["vid"] = usb.vid;
  usbObj["pid"] = usb.pid;
  usbObj["lastFixMs"] = usb.lastFixMs;
  usbObj["connectCount"] = usb.connectCount;
  usbObj["fixCount"] = usb.fixCount;
  usbObj["periodicFixMs"] = usb.periodicFixMs;
  usbObj["lastError"] = usb.lastError;

  JsonObject settingsObj = doc["settings"].to<JsonObject>();
  settingsObj["autoMode"] = settings_.data().autoMode;
  settingsObj["retryIntervalMs"] = settings_.data().retryIntervalMs;
  settingsObj["periodicFixMs"] = settings_.data().periodicFixMs;
  settingsObj["hostname"] = settings_.data().hostname;
  settingsObj["configured"] = settings_.data().configured;

  String out;
  serializeJson(doc, out);
  return out;
}

void HttpServer::broadcastStatus() {
  if (!started_) {
    return;
  }
  String msg =
      String("{\"type\":\"status\",\"data\":") + buildStatusJson() + "}";
  ws_.broadcastTXT(msg);
}

void HttpServer::broadcastLog(const String& level, const String& message,
                              uint32_t t) {
  if (!started_) {
    return;
  }
  JsonDocument doc;
  doc["type"] = "log";
  JsonObject data = doc["data"].to<JsonObject>();
  data["t"] = t;
  data["level"] = level;
  data["msg"] = message;
  String out;
  serializeJson(doc, out);
  ws_.broadcastTXT(out);
}

String HttpServer::contentType(const String& path) const {
  if (path.endsWith(".html")) {
    return "text/html";
  }
  if (path.endsWith(".css")) {
    return "text/css";
  }
  if (path.endsWith(".js")) {
    return "application/javascript";
  }
  if (path.endsWith(".json")) {
    return "application/json";
  }
  if (path.endsWith(".svg")) {
    return "image/svg+xml";
  }
  return "text/plain";
}

bool HttpServer::serveFile(const String& path) {
  if (!LittleFS.exists(path)) {
    return false;
  }
  File file = LittleFS.open(path, "r");
  if (!file) {
    return false;
  }
  server_.streamFile(file, contentType(path));
  file.close();
  return true;
}
