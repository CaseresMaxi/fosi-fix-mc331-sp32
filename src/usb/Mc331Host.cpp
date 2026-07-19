#include "Mc331Host.h"

#include <cstring>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "usb/usb_host.h"

#include "../config/Config.h"
#include "../logger/Logger.h"

namespace {

constexpr uint8_t kHidClass = 0x03;
constexpr uint8_t kHidSetReport = 0x09;
constexpr uint8_t kHidReportTypeOutput = 0x02;
constexpr uint8_t kHidReportTypeFeature = 0x03;

bool parseHidInterface(const usb_config_desc_t* config, uint8_t* ifaceOut) {
  if (config == nullptr || ifaceOut == nullptr) {
    return false;
  }

  const uint8_t* ptr = reinterpret_cast<const uint8_t*>(config);
  int offset = 0;
  while (offset < config->wTotalLength) {
    const auto* desc = reinterpret_cast<const usb_standard_desc_t*>(ptr + offset);
    if (desc->bLength == 0) {
      break;
    }
    if (desc->bDescriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
      const auto* iface = reinterpret_cast<const usb_intf_desc_t*>(desc);
      if (iface->bInterfaceClass == kHidClass) {
        *ifaceOut = iface->bInterfaceNumber;
        return true;
      }
    }
    offset += desc->bLength;
  }
  return false;
}

void transferCallback(usb_transfer_t* transfer) {
  if (transfer != nullptr && transfer->context != nullptr) {
    xSemaphoreGive(static_cast<SemaphoreHandle_t>(transfer->context));
  }
}

}  // namespace

void mc331UsbLibTask(void* arg) {
  auto* self = static_cast<Mc331Host*>(arg);
  while (true) {
    uint32_t flags = 0;
    usb_host_lib_handle_events(portMAX_DELAY, &flags);
    if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
      usb_host_device_free_all();
    }
    (void)self;
  }
}

void mc331UsbClientTask(void* arg) {
  auto* self = static_cast<Mc331Host*>(arg);
  auto client = static_cast<usb_host_client_handle_t>(self->clientHandle_);
  while (true) {
    usb_host_client_handle_events(client, portMAX_DELAY);
  }
}

void mc331ClientEventCallback(const void* eventMsg, void* arg) {
  const auto* event = static_cast<const usb_host_client_event_msg_t*>(eventMsg);
  auto* self = static_cast<Mc331Host*>(arg);
  if (event == nullptr || self == nullptr) {
    return;
  }

  switch (event->event) {
    case USB_HOST_CLIENT_EVENT_NEW_DEV:
      self->handleNewDevice(event->new_dev.address);
      break;
    case USB_HOST_CLIENT_EVENT_DEV_GONE:
      self->handleDeviceGone(event->dev_gone.dev_hdl);
      break;
    default:
      break;
  }
}

static void clientEventCallback(const usb_host_client_event_msg_t* event,
                                void* arg) {
  mc331ClientEventCallback(event, arg);
}

Mc331Host::Mc331Host() {
  status_.state = Mc331State::Idle;
  status_.connected = false;
  status_.fixApplied = false;
  status_.vid = 0;
  status_.pid = 0;
  status_.address = 0;
  status_.lastFixMs = 0;
  status_.connectCount = 0;
  status_.fixCount = 0;
  status_.periodicFixMs = Config::kDefaultPeriodicFixMs;
}

bool Mc331Host::begin() {
  if (hostReady_) {
    return true;
  }

  const usb_host_config_t hostConfig = {
      .skip_phy_setup = false,
      .intr_flags = ESP_INTR_FLAG_LEVEL1,
  };

  esp_err_t err = usb_host_install(&hostConfig);
  if (err != ESP_OK) {
    status_.lastError = esp_err_to_name(err);
    Logger::instance().error(String("USB Host install failed: ") +
                             status_.lastError);
    setState(Mc331State::Error);
    return false;
  }

  BaseType_t ok = xTaskCreatePinnedToCore(mc331UsbLibTask, "usb_lib", 4096, this,
                                          2, &libTask_, 0);
  if (ok != pdPASS) {
    Logger::instance().error("USB lib task create failed");
    setState(Mc331State::Error);
    return false;
  }

  const usb_host_client_config_t clientConfig = {
      .is_synchronous = false,
      .max_num_event_msg = 5,
      .async =
          {
              .client_event_callback = clientEventCallback,
              .callback_arg = this,
          },
  };

  usb_host_client_handle_t client = nullptr;
  err = usb_host_client_register(&clientConfig, &client);
  if (err != ESP_OK) {
    status_.lastError = esp_err_to_name(err);
    Logger::instance().error(String("USB client register failed: ") +
                             status_.lastError);
    setState(Mc331State::Error);
    return false;
  }
  clientHandle_ = client;

  ok = xTaskCreatePinnedToCore(mc331UsbClientTask, "usb_client", 6144, this, 3,
                               &clientTask_, 0);
  if (ok != pdPASS) {
    Logger::instance().error("USB client task create failed");
    setState(Mc331State::Error);
    return false;
  }

  hostReady_ = true;
  setState(Mc331State::Waiting);
  Logger::instance().info("USB Host iniciado");
  Logger::instance().info("Esperando MC331");
  Logger::instance().info(String("Fix periodico cada ") +
                          String(periodicFixMs_ / 1000) + "s");
  return true;
}

void Mc331Host::loop() {
  if (!hostReady_) {
    return;
  }

  const uint32_t now = millis();

  if (now - lastPresencePollMs_ >= Config::kUsbPresencePollMs) {
    lastPresencePollMs_ = now;
    pollPresence();
  }

  if (fixRequested_) {
    fixRequested_ = false;
    if (status_.connected) {
      applyFix();
    } else {
      Logger::instance().warn("Fix solicitado sin MC331 conectado");
    }
  }

  if (fixPending_ && status_.connected && now >= settleUntilMs_) {
    fixPending_ = false;
    if (autoMode_) {
      applyFix();
      lastPeriodicFixMs_ = millis();
    }
  }

  if (autoMode_ && status_.connected && periodicFixMs_ > 0 && !fixPending_) {
    if (now - lastPeriodicFixMs_ >= periodicFixMs_) {
      lastPeriodicFixMs_ = now;
      Logger::instance().info("Reenvio periodico del Fix");
      if (!applyFix()) {
        markDisconnected("fix periodico fallido");
      }
    }
  }

  if (!status_.connected && autoMode_ && status_.state == Mc331State::Error) {
    if (now - lastRetryMs_ >= retryIntervalMs_) {
      lastRetryMs_ = now;
      Logger::instance().info("Reintentando");
      setState(Mc331State::Waiting);
    }
  }
}

bool Mc331Host::addressPresent(uint8_t address) const {
  uint8_t addrs[8] = {};
  int num = 0;
  if (usb_host_device_addr_list_fill(8, addrs, &num) != ESP_OK) {
    return false;
  }
  for (int i = 0; i < num; ++i) {
    if (addrs[i] == address) {
      return true;
    }
  }
  return false;
}

void Mc331Host::pollPresence() {
  uint8_t addrs[8] = {};
  int num = 0;
  if (usb_host_device_addr_list_fill(8, addrs, &num) != ESP_OK) {
    return;
  }

  if (status_.connected) {
    if (!addressPresent(status_.address)) {
      Logger::instance().warn("MC331 apagado o desconectado (poll)");
      markDisconnected("ausente en bus USB");
    }
    return;
  }

  for (int i = 0; i < num; ++i) {
    if (openCompatibleDevice(addrs[i])) {
      Logger::instance().info("MC331 encendido detectado");
      scheduleAutoFix();
      return;
    }
  }
}

void Mc331Host::handleNewDevice(uint8_t address) {
  Logger::instance().info(String("USB device @") + String(address));

  if (status_.connected) {
    if (status_.address == address) {
      return;
    }
    Logger::instance().info("Reconexion detectada");
    closeDevice();
    status_.connected = false;
    status_.fixApplied = false;
  }

  if (openCompatibleDevice(address)) {
    scheduleAutoFix();
  }
}

void Mc331Host::handleDeviceGone(void* goneHandle) {
  if (deviceHandle_ != nullptr && goneHandle != nullptr &&
      deviceHandle_ != goneHandle) {
    return;
  }
  markDisconnected("evento USB DEV_GONE");
}

void Mc331Host::markDisconnected(const char* reason) {
  if (!status_.connected && status_.state == Mc331State::Waiting) {
    closeDevice();
    return;
  }

  Logger::instance().warn(String("MC331 desconectado: ") + reason);
  closeDevice();
  status_.connected = false;
  status_.fixApplied = false;
  status_.vid = 0;
  status_.pid = 0;
  status_.address = 0;
  lastPeriodicFixMs_ = 0;
  setState(Mc331State::Disconnected);
  setState(Mc331State::Waiting);
  Logger::instance().info("Esperando reconexion / encendido");
}

bool Mc331Host::openCompatibleDevice(uint8_t address) {
  auto client = static_cast<usb_host_client_handle_t>(clientHandle_);
  usb_device_handle_t device = nullptr;

  esp_err_t err = usb_host_device_open(client, address, &device);
  if (err != ESP_OK) {
    return false;
  }

  const usb_device_desc_t* desc = nullptr;
  err = usb_host_get_device_descriptor(device, &desc);
  if (err != ESP_OK || desc == nullptr) {
    usb_host_device_close(client, device);
    return false;
  }

  const uint16_t vid = desc->idVendor;
  const uint16_t pid = desc->idProduct;

  if (vid != Config::kMc331Vid || !Config::isCompatiblePid(pid)) {
    usb_host_device_close(client, device);
    return false;
  }

  deviceHandle_ = device;
  status_.vid = vid;
  status_.pid = pid;
  status_.address = address;
  status_.connected = true;
  status_.connectCount += 1;
  status_.fixApplied = false;
  status_.lastError = "";
  status_.periodicFixMs = periodicFixMs_;

  Logger::instance().info(
      String("MC331 detectado VID=0x") + String(vid, HEX) + " PID=0x" +
      String(pid, HEX));

  if (!claimHidInterface()) {
    closeDevice();
    status_.connected = false;
    setState(Mc331State::Error);
    return false;
  }

  setState(Mc331State::Connected);
  notify();
  return true;
}

bool Mc331Host::claimHidInterface() {
  auto client = static_cast<usb_host_client_handle_t>(clientHandle_);
  auto device = static_cast<usb_device_handle_t>(deviceHandle_);

  const usb_config_desc_t* config = nullptr;
  esp_err_t err = usb_host_get_active_config_descriptor(device, &config);
  if (err != ESP_OK || config == nullptr) {
    status_.lastError = "config descriptor";
    Logger::instance().error("USB Error: config descriptor");
    return false;
  }

  uint8_t iface = 0;
  if (!parseHidInterface(config, &iface)) {
    status_.lastError = "HID interface not found";
    Logger::instance().error("USB Error: HID interface not found");
    return false;
  }

  err = usb_host_interface_claim(client, device, iface, 0);
  if (err != ESP_OK) {
    status_.lastError = esp_err_to_name(err);
    Logger::instance().error(String("USB Error: claim failed ") +
                             status_.lastError);
    return false;
  }

  hidInterface_ = iface;
  interfaceClaimed_ = true;

  if (controlTransfer_ == nullptr) {
    usb_transfer_t* transfer = nullptr;
    err = usb_host_transfer_alloc(sizeof(usb_setup_packet_t) +
                                      Config::kHidReportSize,
                                  0, &transfer);
    if (err != ESP_OK || transfer == nullptr) {
      status_.lastError = "transfer alloc";
      Logger::instance().error("USB Error: transfer alloc");
      return false;
    }
    controlTransfer_ = transfer;
  }

  return true;
}

bool Mc331Host::sendHidReport(const uint8_t* data, size_t length) {
  if (!status_.connected || !interfaceClaimed_ || controlTransfer_ == nullptr ||
      data == nullptr || length == 0) {
    return false;
  }

  auto client = static_cast<usb_host_client_handle_t>(clientHandle_);
  auto device = static_cast<usb_device_handle_t>(deviceHandle_);
  auto* transfer = static_cast<usb_transfer_t*>(controlTransfer_);

  SemaphoreHandle_t done = xSemaphoreCreateBinary();
  if (done == nullptr) {
    return false;
  }

  auto trySend = [&](uint8_t reportType) -> bool {
    auto* setup = reinterpret_cast<usb_setup_packet_t*>(transfer->data_buffer);
    const uint8_t reportId = data[0];
    const uint8_t* payload = data;
    size_t payloadLen = length;

    if (length > 1 && reportId == 0x00) {
      payload = data + 1;
      payloadLen = length - 1;
    }

    setup->bmRequestType = USB_BM_REQUEST_TYPE_DIR_OUT |
                           USB_BM_REQUEST_TYPE_TYPE_CLASS |
                           USB_BM_REQUEST_TYPE_RECIP_INTERFACE;
    setup->bRequest = kHidSetReport;
    setup->wValue = (static_cast<uint16_t>(reportType) << 8) | reportId;
    setup->wIndex = hidInterface_;
    setup->wLength = payloadLen;

    memcpy(transfer->data_buffer + sizeof(usb_setup_packet_t), payload,
           payloadLen);

    transfer->device_handle = device;
    transfer->bEndpointAddress = 0;
    transfer->callback = transferCallback;
    transfer->context = done;
    transfer->timeout_ms = 1000;
    transfer->num_bytes = sizeof(usb_setup_packet_t) + payloadLen;

    xSemaphoreTake(done, 0);
    esp_err_t err = usb_host_transfer_submit_control(client, transfer);
    if (err != ESP_OK) {
      return false;
    }

    if (xSemaphoreTake(done, pdMS_TO_TICKS(1500)) != pdTRUE) {
      return false;
    }

    return transfer->status == USB_TRANSFER_STATUS_COMPLETED;
  };

  bool ok = trySend(kHidReportTypeOutput);
  if (!ok) {
    Logger::instance().warn("Output report failed, trying Feature");
    ok = trySend(kHidReportTypeFeature);
  }

  vSemaphoreDelete(done);
  return ok;
}

bool Mc331Host::applyFix() {
  if (!status_.connected) {
    Logger::instance().warn("applyFix: MC331 no conectado");
    return false;
  }

  if (!addressPresent(status_.address)) {
    markDisconnected("dispositivo no presente al aplicar Fix");
    return false;
  }

  setState(Mc331State::Applying);
  Logger::instance().info("Enviando paquete HID");

  const bool ok =
      sendHidReport(Config::hidFixPacket(), Config::kHidReportSize);

  if (ok) {
    status_.fixApplied = true;
    status_.lastFixMs = millis();
    status_.fixCount += 1;
    status_.lastError = "";
    status_.periodicFixMs = periodicFixMs_;
    Logger::instance().info("Fix aplicado");
    setState(Mc331State::Fixed);
  } else {
    status_.lastError = "HID transfer failed";
    Logger::instance().error("Error USB: Fix fallido");
    setState(Mc331State::Error);
  }

  notify();
  return ok;
}

bool Mc331Host::requestFix() {
  fixRequested_ = true;
  return true;
}

void Mc331Host::scheduleAutoFix() {
  if (!autoMode_) {
    return;
  }
  fixPending_ = true;
  settleUntilMs_ = millis() + Config::kUsbBootSettleMs;
  lastPeriodicFixMs_ = settleUntilMs_;
  Logger::instance().info("Esperando settle del DSP");
}

void Mc331Host::closeDevice() {
  auto client = static_cast<usb_host_client_handle_t>(clientHandle_);
  auto device = static_cast<usb_device_handle_t>(deviceHandle_);

  if (interfaceClaimed_ && client != nullptr && device != nullptr) {
    usb_host_interface_release(client, device, hidInterface_);
  }
  interfaceClaimed_ = false;
  hidInterface_ = 0;

  if (device != nullptr && client != nullptr) {
    usb_host_device_close(client, device);
  }
  deviceHandle_ = nullptr;
  fixPending_ = false;
}

void Mc331Host::setState(Mc331State state) {
  portENTER_CRITICAL(&mux_);
  status_.state = state;
  portEXIT_CRITICAL(&mux_);
  notify();
}

void Mc331Host::notify() {
  if (eventCb_) {
    eventCb_(status());
  }
}

Mc331Status Mc331Host::status() const {
  portENTER_CRITICAL(&mux_);
  Mc331Status copy = status_;
  copy.periodicFixMs = periodicFixMs_;
  portEXIT_CRITICAL(&mux_);
  return copy;
}

bool Mc331Host::isConnected() const { return status_.connected; }

Mc331State Mc331Host::state() const { return status_.state; }

void Mc331Host::setAutoMode(bool enabled) { autoMode_ = enabled; }

void Mc331Host::setRetryIntervalMs(uint32_t intervalMs) {
  retryIntervalMs_ = intervalMs < 500 ? 500 : intervalMs;
}

void Mc331Host::setPeriodicFixMs(uint32_t intervalMs) {
  if (intervalMs > 0 && intervalMs < 5000) {
    intervalMs = 5000;
  }
  periodicFixMs_ = intervalMs;
  status_.periodicFixMs = periodicFixMs_;
}

void Mc331Host::onEvent(Mc331EventCallback callback) {
  eventCb_ = std::move(callback);
}
