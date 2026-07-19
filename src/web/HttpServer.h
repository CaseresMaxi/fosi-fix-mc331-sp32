#pragma once

#include <Arduino.h>
#include <DNSServer.h>
#include <Update.h>
#include <WebServer.h>
#include <WebSocketsServer.h>

#include "../storage/Settings.h"
#include "../usb/Mc331Host.h"
#include "../wifi/WifiManager.h"

class HttpServer {
 public:
  HttpServer(Settings& settings, WifiManager& wifi, Mc331Host& usb);

  bool begin();
  void loop();

  void broadcastStatus();
  void broadcastLog(const String& level, const String& message, uint32_t t);

 private:
  void setupRoutes();
  void handleRoot();
  void handleStatus();
  void handleLogs();
  void handleFix();
  void handleReboot();
  void handleSettingsGet();
  void handleSettingsPost();
  void handleWifiScan();
  void handleOta();
  void handleNotFound();
  void sendFallbackUi();

  String buildStatusJson() const;
  String contentType(const String& path) const;
  bool serveFile(const String& path);

  Settings& settings_;
  WifiManager& wifi_;
  Mc331Host& usb_;
  WebServer server_;
  WebSocketsServer ws_;
  DNSServer dns_;
  bool started_ = false;
  bool dnsActive_ = false;
  uint32_t bootMs_ = 0;
  uint32_t lastStatusBroadcastMs_ = 0;
};
