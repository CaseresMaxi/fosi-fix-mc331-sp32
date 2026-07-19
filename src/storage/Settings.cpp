#include "Settings.h"

bool Settings::begin() {
  ready_ = prefs_.begin(Config::kPreferencesNamespace, false);
  if (ready_) {
    load();
  }
  return ready_;
}

void Settings::applyDefaults() {
  data_.ssid = "";
  data_.password = "";
  data_.hostname = Config::kDefaultHostname;
  data_.autoMode = true;
  data_.retryIntervalMs = Config::kDefaultRetryIntervalMs;
  data_.periodicFixMs = Config::kDefaultPeriodicFixMs;
  data_.configured = false;
}

void Settings::load() {
  applyDefaults();
  if (!ready_) {
    return;
  }

  if (prefs_.isKey("ssid")) {
    data_.ssid = prefs_.getString("ssid", "");
  }
  if (prefs_.isKey("pass")) {
    data_.password = prefs_.getString("pass", "");
  }
  if (prefs_.isKey("host")) {
    data_.hostname = prefs_.getString("host", Config::kDefaultHostname);
  }
  if (prefs_.isKey("auto")) {
    data_.autoMode = prefs_.getBool("auto", true);
  }
  if (prefs_.isKey("retry")) {
    data_.retryIntervalMs =
        prefs_.getUInt("retry", Config::kDefaultRetryIntervalMs);
  }
  if (prefs_.isKey("pfix")) {
    data_.periodicFixMs =
        prefs_.getUInt("pfix", Config::kDefaultPeriodicFixMs);
  }
  if (prefs_.isKey("cfg")) {
    data_.configured = prefs_.getBool("cfg", false);
  }

  if (data_.hostname.isEmpty()) {
    data_.hostname = Config::kDefaultHostname;
  }
  if (data_.retryIntervalMs < 500) {
    data_.retryIntervalMs = Config::kDefaultRetryIntervalMs;
  }
  if (data_.periodicFixMs > 0 && data_.periodicFixMs < 5000) {
    data_.periodicFixMs = Config::kDefaultPeriodicFixMs;
  }
}

bool Settings::save() {
  if (!ready_) {
    return false;
  }

  prefs_.putString("ssid", data_.ssid);
  prefs_.putString("pass", data_.password);
  prefs_.putString("host", data_.hostname);
  prefs_.putBool("auto", data_.autoMode);
  prefs_.putUInt("retry", data_.retryIntervalMs);
  prefs_.putUInt("pfix", data_.periodicFixMs);
  prefs_.putBool("cfg", data_.configured);
  return true;
}

bool Settings::clearWifi() {
  data_.ssid = "";
  data_.password = "";
  data_.configured = false;
  return save();
}

bool Settings::hasWifiCredentials() const {
  return data_.configured && data_.ssid.length() > 0;
}
