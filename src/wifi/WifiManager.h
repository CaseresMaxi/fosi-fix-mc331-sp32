#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <functional>

#include "../storage/Settings.h"

enum class WifiModeState : uint8_t {
  Off = 0,
  Connecting,
  Station,
  AccessPoint,
  Failed
};

struct WifiStatus {
  WifiModeState mode;
  bool connected;
  bool setupMode;
  String ssid;
  String ip;
  String hostname;
  int8_t rssi;
};

using WifiConnectedCallback = std::function<void(const WifiStatus&)>;

class WifiManager {
 public:
  explicit WifiManager(Settings& settings);

  bool begin();
  void loop();
  void reconnect();

  WifiStatus status() const;
  bool isStation() const;
  bool isAccessPoint() const;
  bool isSetupMode() const;
  bool isReadyForServices() const;

  String scanNetworksJson();
  void onConnected(WifiConnectedCallback callback);

 private:
  bool startStation();
  bool startSetupAccessPoint();
  void startMdns();
  void stopMdns();

  Settings& settings_;
  WifiModeState mode_ = WifiModeState::Off;
  bool setupMode_ = false;
  bool mdnsStarted_ = false;
  uint32_t lastAttemptMs_ = 0;
  WifiConnectedCallback connectedCb_;
};
