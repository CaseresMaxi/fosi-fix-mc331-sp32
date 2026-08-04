#pragma once

#include <Arduino.h>
#include <cstdint>

#ifndef FOSIFIX_VERSION
#define FOSIFIX_VERSION "1.0.0"
#endif

namespace Config {

constexpr const char* kAppName = "FosiFix";
constexpr const char* kVersion = FOSIFIX_VERSION;
constexpr const char* kDefaultHostname = "fosifix";
constexpr const char* kApSsid = "FosiFix Setup";
constexpr const char* kApPassword = "";
constexpr const char* kPreferencesNamespace = "fosifix";

constexpr uint16_t kMc331Vid = 0x8888;
constexpr uint16_t kMc331PidUsb = 0x1717;
constexpr uint16_t kMc331PidOpt = 0x171E;

constexpr uint32_t kSerialBaud = 115200;
constexpr uint16_t kHttpPort = 80;
constexpr uint16_t kWsPort = 81;
constexpr size_t kLogCapacity = 80;
constexpr size_t kLogLineMax = 160;
constexpr uint32_t kDefaultRetryIntervalMs = 3000;
constexpr uint32_t kDefaultPeriodicFixMs = 30000;
constexpr uint32_t kUsbBootSettleMs = 3000;
constexpr uint32_t kUsbPresencePollMs = 1000;
constexpr uint32_t kWifiConnectTimeoutMs = 20000;
constexpr uint32_t kStatusBroadcastIntervalMs = 2000;

constexpr size_t kHidReportSize = 65;

inline bool isCompatiblePid(uint16_t pid) {
  return pid == kMc331PidUsb || pid == kMc331PidOpt;
}

inline const uint8_t* hidFixPacket() {
  static const uint8_t kPacket[kHidReportSize] = {
      0x00, 0xA5, 0x5A, 0x88, 0x0B, 0xFF, 0x00, 0x00, 0xD8, 0xDC, 0x03,
      0x00, 0x05, 0x00, 0x64, 0x00, 0x18};
  return kPacket;
}

}  // namespace Config
