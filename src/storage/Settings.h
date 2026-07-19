#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include "../config/Config.h"

struct DeviceSettings {
  String ssid;
  String password;
  String hostname;
  bool autoMode;
  uint32_t retryIntervalMs;
  uint32_t periodicFixMs;
  bool configured;
};

class Settings {
 public:
  bool begin();
  void load();
  bool save();
  bool clearWifi();

  DeviceSettings& data() { return data_; }
  const DeviceSettings& data() const { return data_; }

  bool hasWifiCredentials() const;
  void applyDefaults();

 private:
  Preferences prefs_;
  DeviceSettings data_;
  bool ready_ = false;
};
