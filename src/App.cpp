#include "App.h"

#include "config/Config.h"

App::App() : wifi_(settings_), http_(settings_, wifi_, usb_) {}

void App::begin() {
  Serial.begin(Config::kSerialBaud);
  delay(800);

  Logger::instance().begin(Config::kLogCapacity);
  Logger::instance().info(String(Config::kAppName) + " " + Config::kVersion);
  Logger::instance().info("Inicializando");
  Logger::instance().info("Logs via USB-Serial (CH343), no USB-OTG");

  if (!settings_.begin()) {
    Logger::instance().error("Settings init failed");
  } else {
    Logger::instance().info("Config listo");
  }

  usb_.setAutoMode(settings_.data().autoMode);
  usb_.setRetryIntervalMs(settings_.data().retryIntervalMs);
  usb_.setPeriodicFixMs(settings_.data().periodicFixMs);

  wifi_.begin();

  if (wifi_.isReadyForServices()) {
    http_.begin();
  }

  Logger::instance().addListener([this](const LogEntry& entry) {
    http_.broadcastLog(Logger::levelName(entry.level), entry.message,
                       entry.millis);
  });

  Logger::instance().info("Iniciando USB Host (OTG)");
  if (!usb_.begin()) {
    Logger::instance().error("USB Host no disponible");
  }
}

void App::loop() {
  wifi_.loop();
  usb_.loop();
  http_.loop();
}
