# FosiFix

**FosiFix** is a small **ESP32-S3** device that stays connected to a **Fosi Audio MC331** amplifier and automatically fixes the low-volume audio cut-off (aggressive DSP noise gate).

When the amp powers on, its MCU reloads factory DSP defaults. FosiFix detects the amp over USB and re-applies the community workaround (~**-90 dB** threshold) — automatically, every time, without keeping a PC attached.

> Independent open-source project. Not affiliated with Fosi Audio.

---

## What you need

| Item | Details |
|------|---------|
| Board | **ESP32-S3** with plenty of flash (recommended **N16R8**: 16 MB flash + 8 MB PSRAM) and **USB OTG** |
| Cables | 1) USB **data** cable to the PC (programming port) · 2) USB-C cable to the **MC331** (ESP32 OTG port) |
| PC | Linux or macOS (Windows: use [PlatformIO IDE](https://platformio.org/platformio-ide)) |
| Amplifier | Fosi Audio **MC331** |

### Important: two USB ports on the board

Many ESP32-S3 boards have **two** USB connectors:

| Port | Use it for |
|------|------------|
| **UART / COM / Serial** (often CH340/CH343) | **Flashing** FosiFix and viewing logs |
| **USB / OTG** | Connecting to the **MC331** (USB Host mode) |

Flashing through the OTG port usually fails or goes silent. Always use the **programming** port.

---

## Quick install (recommended)

On Linux or macOS, from the project folder:

```bash
chmod +x scripts/setup_and_flash.sh
./scripts/setup_and_flash.sh
```

The script will try to:

1. Install **PlatformIO** if needed  
2. Warn about USB permissions (`dialout` on Linux)  
3. Build the firmware  
4. Detect the serial port and flash  
5. Print the WiFi setup steps  

Useful options:

```bash
./scripts/setup_and_flash.sh --build-only
./scripts/setup_and_flash.sh --port /dev/ttyUSB0
./scripts/setup_and_flash.sh --monitor
./scripts/setup_and_flash.sh --help
```

---

## Step-by-step guide (non-technical)

### 1. Get the project

- Option A: clone the repository with Git  
- Option B: on GitHub, **Code → Download ZIP** and unzip it

### 2. Connect the ESP32 to your PC

1. Use a USB cable that supports **data** (charge-only cables will not work).  
2. Plug into the ESP32 **programming** port (UART/COM), not OTG.  
3. On Linux, if flashing says “Permission denied”:

```bash
sudo usermod -aG dialout $USER
```

Then **log out** (or reboot) and try again.

### 3. Flash FosiFix

```bash
cd fosi-fix-mc331-sp32
chmod +x scripts/setup_and_flash.sh
./scripts/setup_and_flash.sh
```

Wait until it reports that flashing finished.

### 4. Configure WiFi (first boot)

1. On your phone or PC, join the WiFi network **`FosiFix Setup`**.  
2. Connect (no password).  
3. Open **`http://192.168.4.1`** in a browser.  
4. Pick your home WiFi, enter the password, and save.  
5. The ESP32 reboots and joins your network.  
6. Switch back to your normal WiFi.  
7. Open **`http://fosifix.local`**.

If `fosifix.local` does not open (common on some Android phones or older routers), find the device named `fosifix` in your router’s client list and open its IP (`http://192.168.x.x`).

### 5. Connect the amplifier

1. Connect the ESP32 **USB OTG** port to the **MC331 USB-C** port.  
2. Power on the amplifier.  
3. Wait a few seconds — FosiFix applies the Fix automatically.  
4. In the web UI you should see the MC331 connected and the Fix applied.

Done. Every time the amp reboots or reconnects, FosiFix reapplies the setting.

---

## Daily use

- Leave the ESP32 powered and connected to the MC331.  
- Open `http://fosifix.local` to check status or press **Apply Fix now**.  
- Auto mode periodically resends the Fix (default every 30 s) in case the amp reloads factory defaults.

---

## Build manually (advanced)

With [PlatformIO](https://platformio.org/) installed:

```bash
pio run
pio run -t upload --upload-port /dev/ttyUSB0
pio device monitor --port /dev/ttyUSB0
```

The web UI is **embedded in the firmware** at build time (no `uploadfs` required).

---

## What the firmware does

- Detects the MC331 over USB (`VID 0x8888`, `PID 0x1717` or `0x171E`)  
- Waits for the DSP to finish booting  
- Sends the Noise Suppressor (`0x88`) HID packet with threshold **~-90 dB**  
- Reapplies on reconnect and on a periodic timer  
- Provides WiFi + web UI + OTA + logs

---

## API reference

| Method | Path | Description |
|--------|------|-------------|
| `GET` | `/api/status` | USB, WiFi, memory status |
| `GET` | `/api/logs` | Recent logs |
| `POST` | `/api/fix` | Queue a Fix apply |
| `POST` | `/api/reboot` | Reboot |
| `GET`/`POST` | `/api/settings` | Settings |
| `GET` | `/api/wifi/scan` | Scan WiFi networks |
| `POST` | `/api/ota` | Over-the-air firmware update |

WebSocket on port **81** (`status` + `log`).

---

## Compatible hardware

- Board: ESP32-S3-WROOM-1 **N16R8** (or another S3 with OTG + enough flash)  
- Amplifier: Fosi Audio MC331  
- Power: board / programming USB; OTG talks to the amp

---

## Troubleshooting

**No board detected while flashing**  
Charge-only cable, OTG instead of UART, or missing `dialout` permissions.

**Web UI blank / broken**  
Flash again with the script (UI is inside the firmware). Hard-refresh the browser.

**`fosifix.local` does not work**  
Use the IP from your router. mDNS often fails on phones.

**Fix does not apply**  
Confirm OTG → MC331 cable, amp powered on, and web UI shows MC331 connected. Try **Apply Fix now**.

**Serial monitor stays blank**  
You are on the OTG port. Open the monitor on the UART/COM port.

---

## Project layout

```
src/           firmware (USB Host, WiFi, web, settings)
data/          web UI (embedded at build time)
scripts/       setup_and_flash.sh + embed_ui.py
platformio.ini PlatformIO config
```

---

## License

MIT — see [LICENSE](LICENSE).

## Credits

- Community Low Volume Cut-Off Fix research (Fosi Audio forum / ACP Workbench)  
- Espressif USB Host Library  
- FosiFix contributors

## Disclaimer

Use at your own risk. This does not permanently rewrite MC331 internal firmware: the setting is reapplied over USB while FosiFix remains connected.
