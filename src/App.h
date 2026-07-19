#pragma once

#include "logger/Logger.h"
#include "storage/Settings.h"
#include "usb/Mc331Host.h"
#include "web/HttpServer.h"
#include "wifi/WifiManager.h"

class App {
 public:
  App();

  void begin();
  void loop();

 private:
  Settings settings_;
  WifiManager wifi_;
  Mc331Host usb_;
  HttpServer http_;
};
