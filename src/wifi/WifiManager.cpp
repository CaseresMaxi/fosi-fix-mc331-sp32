#include "WifiManager.h"

#include <ESPmDNS.h>
#include <WiFi.h>

#include "../config/Config.h"
#include "../logger/Logger.h"

WifiManager::WifiManager(Settings& settings) : settings_(settings) {}

bool WifiManager::begin() {
  WiFi.persistent(false);
  WiFi.setSleep(false);

  if (settings_.hasWifiCredentials()) {
    return startStation();
  }
  return startSetupAccessPoint();
}

bool WifiManager::startStation() {
  mode_ = WifiModeState::Connecting;
  setupMode_ = false;
  stopMdns();

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(settings_.data().hostname.c_str());
  WiFi.begin(settings_.data().ssid.c_str(), settings_.data().password.c_str());

  Logger::instance().info("WiFi connecting to " + settings_.data().ssid);

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED &&
         (millis() - start) < Config::kWifiConnectTimeoutMs) {
    delay(200);
  }

  if (WiFi.status() != WL_CONNECTED) {
    mode_ = WifiModeState::Failed;
    Logger::instance().error("WiFi connection failed, starting setup AP");
    return startSetupAccessPoint();
  }

  mode_ = WifiModeState::Station;
  Logger::instance().info("WiFi connected");
  Logger::instance().info("IP " + WiFi.localIP().toString());
  Logger::instance().info("Open http://" + settings_.data().hostname +
                          ".local from your home WiFi");
  startMdns();

  if (connectedCb_) {
    connectedCb_(status());
  }
  return true;
}

bool WifiManager::startSetupAccessPoint() {
  mode_ = WifiModeState::AccessPoint;
  setupMode_ = true;
  stopMdns();

  WiFi.mode(WIFI_AP_STA);
  WiFi.disconnect(false, true);
  delay(100);

  const bool ok = WiFi.softAP(Config::kApSsid, Config::kApPassword);
  if (!ok) {
    Logger::instance().error("Failed to start Access Point");
    mode_ = WifiModeState::Failed;
    setupMode_ = false;
    return false;
  }

  Logger::instance().info(String("Setup AP: ") + Config::kApSsid);
  Logger::instance().info("AP IP " + WiFi.softAPIP().toString());
  Logger::instance().info("Join this AP once, pick your home WiFi, then return to your network");
  startMdns();

  if (connectedCb_) {
    connectedCb_(status());
  }
  return true;
}

void WifiManager::stopMdns() {
  if (mdnsStarted_) {
    MDNS.end();
    mdnsStarted_ = false;
  }
}

void WifiManager::startMdns() {
  stopMdns();
  const String& host = settings_.data().hostname;
  if (MDNS.begin(host.c_str())) {
    MDNS.addService("http", "tcp", Config::kHttpPort);
    mdnsStarted_ = true;
    Logger::instance().info("mDNS started: http://" + host + ".local");
  } else {
    Logger::instance().warn("mDNS start failed");
  }
}

void WifiManager::loop() {
  if (mode_ == WifiModeState::Station && WiFi.status() != WL_CONNECTED) {
    const uint32_t now = millis();
    if (now - lastAttemptMs_ < settings_.data().retryIntervalMs) {
      return;
    }
    lastAttemptMs_ = now;
    Logger::instance().warn("WiFi disconnected, reconnecting");
    reconnect();
  }
}

void WifiManager::reconnect() {
  if (settings_.hasWifiCredentials()) {
    startStation();
  }
}

String WifiManager::scanNetworksJson() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();

  const wifi_mode_t previous = WiFi.getMode();
  if (previous == WIFI_AP) {
    WiFi.mode(WIFI_AP_STA);
    delay(50);
  }

  Logger::instance().info("Scanning WiFi networks");
  const int n = WiFi.scanNetworks(/*async=*/false, /*hidden=*/false);
  if (n < 0) {
    Logger::instance().error("WiFi scan failed");
    String out;
    serializeJson(arr, out);
    return out;
  }

  for (int i = 0; i < n; ++i) {
    JsonObject net = arr.add<JsonObject>();
    net["ssid"] = WiFi.SSID(i);
    net["rssi"] = WiFi.RSSI(i);
    net["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    net["channel"] = WiFi.channel(i);
  }
  WiFi.scanDelete();
  Logger::instance().info(String("WiFi scan found ") + String(n) + " networks");

  String out;
  serializeJson(arr, out);
  return out;
}

WifiStatus WifiManager::status() const {
  WifiStatus st;
  st.mode = mode_;
  st.setupMode = setupMode_;
  st.hostname = settings_.data().hostname;
  st.ssid = "";
  st.ip = "";
  st.rssi = 0;
  st.connected = false;

  if (mode_ == WifiModeState::Station && WiFi.status() == WL_CONNECTED) {
    st.connected = true;
    st.ssid = WiFi.SSID();
    st.ip = WiFi.localIP().toString();
    st.rssi = WiFi.RSSI();
  } else if (mode_ == WifiModeState::AccessPoint) {
    st.connected = true;
    st.ssid = Config::kApSsid;
    st.ip = WiFi.softAPIP().toString();
  }

  return st;
}

bool WifiManager::isStation() const {
  return mode_ == WifiModeState::Station && WiFi.status() == WL_CONNECTED;
}

bool WifiManager::isAccessPoint() const {
  return mode_ == WifiModeState::AccessPoint;
}

bool WifiManager::isSetupMode() const { return setupMode_; }

bool WifiManager::isReadyForServices() const {
  return isStation() || isAccessPoint();
}

void WifiManager::onConnected(WifiConnectedCallback callback) {
  connectedCb_ = std::move(callback);
}
