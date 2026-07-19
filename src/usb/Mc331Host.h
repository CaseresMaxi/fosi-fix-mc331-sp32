#pragma once

#include <Arduino.h>
#include <functional>

enum class Mc331State : uint8_t {
  Idle = 0,
  Waiting,
  Connected,
  Applying,
  Fixed,
  Error,
  Disconnected
};

struct Mc331Status {
  Mc331State state;
  bool connected;
  bool fixApplied;
  uint16_t vid;
  uint16_t pid;
  uint8_t address;
  uint32_t lastFixMs;
  uint32_t connectCount;
  uint32_t fixCount;
  uint32_t periodicFixMs;
  String lastError;
};

using Mc331EventCallback = std::function<void(const Mc331Status&)>;

class Mc331Host {
 public:
  Mc331Host();

  bool begin();
  void loop();

  bool applyFix();
  bool requestFix();

  Mc331Status status() const;
  bool isConnected() const;
  Mc331State state() const;

  void setAutoMode(bool enabled);
  void setRetryIntervalMs(uint32_t intervalMs);
  void setPeriodicFixMs(uint32_t intervalMs);
  void onEvent(Mc331EventCallback callback);

 private:
  friend void mc331UsbLibTask(void* arg);
  friend void mc331UsbClientTask(void* arg);
  friend void mc331ClientEventCallback(const void* event, void* arg);

  void handleNewDevice(uint8_t address);
  void handleDeviceGone(void* goneHandle = nullptr);
  void markDisconnected(const char* reason);
  void pollPresence();
  bool addressPresent(uint8_t address) const;
  bool openCompatibleDevice(uint8_t address);
  bool claimHidInterface();
  bool sendHidReport(const uint8_t* data, size_t length);
  void closeDevice();
  void setState(Mc331State state);
  void notify();
  void scheduleAutoFix();

  Mc331Status status_{};
  bool autoMode_ = true;
  bool fixRequested_ = false;
  bool fixPending_ = false;
  uint32_t retryIntervalMs_ = 3000;
  uint32_t periodicFixMs_ = 30000;
  uint32_t settleUntilMs_ = 0;
  uint32_t lastRetryMs_ = 0;
  uint32_t lastPresencePollMs_ = 0;
  uint32_t lastPeriodicFixMs_ = 0;
  uint8_t hidInterface_ = 0;
  bool interfaceClaimed_ = false;
  bool hostReady_ = false;

  void* clientHandle_ = nullptr;
  void* deviceHandle_ = nullptr;
  void* controlTransfer_ = nullptr;

  Mc331EventCallback eventCb_;
  mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  TaskHandle_t libTask_ = nullptr;
  TaskHandle_t clientTask_ = nullptr;
};
