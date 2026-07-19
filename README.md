# FosiFix

ESP32-S3 USB Host module that permanently applies the community **Low Volume Cut-Off Fix** to the [Fosi Audio MC331](https://fosiaudio.com/).

When the amplifier boots, its MCU reloads factory DSP defaults and re-enables an aggressive noise gate. FosiFix detects the amp over USB, waits for DSP settle, and injects the known HID payload that disables it — automatically, on every connect.

## Hardware

| Item | Notes |
|------|--------|
| MCU | ESP32-S3-WROOM-1 (N16R8: 16 MB Flash + 8 MB PSRAM) |
| USB OTG | USB Host → MC331 |
| USB Serial (CH343) | Programming + Serial logs only |
| Network | WiFi (+ mDNS `fosifix.local`) |

Compatible MC331 USB IDs:

- `VID 0x8888` / `PID 0x1717` (USB mode)
- `VID 0x8888` / `PID 0x171E` (OPT / AUX control)

## Features

- Automatic MC331 detection, fix, disconnect / reconnect handling
- Modular architecture ready for DSP tools, MQTT, Home Assistant, etc.
- Captive-style first-boot WiFi setup AP: **FosiFix Setup**
- Web UI at `http://fosifix.local`
- REST API + WebSocket live logs
- OTA firmware update from the browser
- Preferences-backed settings (SSID, password, hostname, auto mode, retry interval)

## Project layout

```
src/
  main.cpp
  App.*
  config/     compile-time constants + HID packet
  storage/    Preferences settings
  logger/     Serial + ring buffer + listeners
  wifi/       STA / AP + mDNS
  usb/        Mc331Host (USB Host + applyFix)
  web/        HTTP, REST, WebSocket, OTA
data/         LittleFS web UI (HTML/CSS/JS)
```

## Build

Requires [PlatformIO](https://platformio.org/).

```bash
pio run
pio run -t upload
pio run -t uploadfs
pio device monitor
```

`uploadfs` flashes the web UI into LittleFS. Without it the API still works, but the UI returns an error page.

### Important: which USB port?

Boards with **two USB connectors**:

| Port | Chip / role | Use for |
|------|-------------|---------|
| **COM / Serial** (CH343) | UART → usually `/dev/ttyUSB0` | **Upload + Serial monitor** |
| **USB / OTG** | ESP32-S3 native USB → `/dev/ttyACM0` | **MC331 only** (USB Host) |

If the monitor is on `/dev/ttyACM0` and stays blank, you are on the wrong cable. After `usb_host_install()` the native USB PHY becomes Host and CDC/JTAG on that port goes silent.

```bash
pio device list
pio run -t upload --upload-port /dev/ttyUSB0
pio device monitor --port /dev/ttyUSB0
```

## First boot

1. Power the board.
2. Join WiFi AP **FosiFix Setup**.
3. Open `http://192.168.4.1` (or `http://fosifix.local`).
4. Enter your SSID / password and save (device reboots into station mode).
5. Connect ESP32 USB-OTG to the MC331 USB-C port.
6. FosiFix applies the fix automatically after a short settle delay.

## REST API

| Method | Path | Description |
|--------|------|-------------|
| `GET` | `/api/status` | Device, USB, WiFi, memory |
| `GET` | `/api/logs` | Recent log ring buffer |
| `POST` | `/api/fix` | Queue `applyFix()` |
| `POST` | `/api/reboot` | Restart |
| `POST` | `/api/settings` | JSON settings (reboot by default) |
| `POST` | `/api/ota` | Multipart firmware upload |

WebSocket live feed: `ws://<ip>:81/` (`status` + `log` messages).

## HID payload

Exact 65-byte report (zeros padded). Do not modify:

```
00 A5 5A 88 0B FF 00 00
70 E5 03 00 05 00 64 00
16
(+ zeros to 65 bytes)
```

## Extending

`Mc331Host` owns USB life-cycle. Future DSP register R/W, noise-gate UI, EQ, backups, MQTT, or Home Assistant should plug in as new modules beside `usb/`, `web/`, and `wifi/` — orchestrated from `App`, without rewriting the core.

## License

Open source — choose a license before publishing (MIT / Apache-2.0 recommended).

## Credits

- Community Low Volume Cut-Off Fix research on the Fosi Audio forums
- Espressif USB Host Library
