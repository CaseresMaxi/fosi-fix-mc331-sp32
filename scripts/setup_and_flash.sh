#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

RED=$'\033[0;31m'
GREEN=$'\033[0;32m'
YELLOW=$'\033[1;33m'
CYAN=$'\033[0;36m'
BOLD=$'\033[1m'
NC=$'\033[0m'

say() { printf '%s%s%s\n' "$CYAN" "$*" "$NC"; }
ok() { printf '%s%s%s\n' "$GREEN" "$*" "$NC"; }
warn() { printf '%s%s%s\n' "$YELLOW" "$*" "$NC"; }
die() { printf '%s%s%s\n' "$RED" "$*" "$NC" >&2; exit 1; }

usage() {
  cat <<'EOF'
FosiFix — automatic setup and flash

Usage:
  ./scripts/setup_and_flash.sh              # install deps, build, and flash
  ./scripts/setup_and_flash.sh --build-only # build only
  ./scripts/setup_and_flash.sh --port /dev/ttyUSB0
  ./scripts/setup_and_flash.sh --monitor    # flash, then open serial monitor

Minimum requirements:
  - Linux or macOS
  - Python 3
  - USB data cable (not charge-only)
  - ESP32-S3 N16R8 board (or compatible)
EOF
}

BUILD_ONLY=0
DO_MONITOR=0
UPLOAD_PORT=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help) usage; exit 0 ;;
    --build-only) BUILD_ONLY=1; shift ;;
    --monitor) DO_MONITOR=1; shift ;;
    --port)
      [[ $# -ge 2 ]] || die "Missing value for --port"
      UPLOAD_PORT="$2"
      shift 2
      ;;
    *) die "Unknown option: $1 (try --help)" ;;
  esac
done

say "${BOLD}=== FosiFix setup ===${NC}"
say "Project: $ROOT"

need_cmd() {
  command -v "$1" >/dev/null 2>&1
}

install_platformio() {
  export PATH="$HOME/.platformio/penv/bin:$HOME/.local/bin:$PATH"

  if need_cmd pio; then
    ok "PlatformIO CLI found: $(command -v pio)"
    return
  fi
  if need_cmd platformio; then
    ok "PlatformIO CLI found: $(command -v platformio)"
    return
  fi

  warn "PlatformIO is not installed. Trying to install it..."
  if ! need_cmd python3; then
    die "Python 3 is required. Install it and run this script again."
  fi

  if need_cmd curl; then
    curl -fsSL https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py -o /tmp/get-platformio.py
    python3 /tmp/get-platformio.py || \
      python3 -m pip install --user -U platformio || \
      die "Could not install PlatformIO. See https://platformio.org/install/cli"
  else
    python3 -m pip install --user -U platformio || \
      die "Could not install PlatformIO with pip. See https://platformio.org/install/cli"
  fi

  export PATH="$HOME/.platformio/penv/bin:$HOME/.local/bin:$PATH"
  if need_cmd pio || need_cmd platformio; then
    ok "PlatformIO installed"
  else
    die "PlatformIO installed but not on PATH. Open a new terminal and retry."
  fi
}

pio_cmd() {
  if need_cmd pio; then
    pio "$@"
  else
    platformio "$@"
  fi
}

check_serial_permission() {
  if [[ "$(uname -s)" != "Linux" ]]; then
    return
  fi
  if id -nG "$USER" | tr ' ' '\n' | grep -qx dialout; then
    ok "Serial permissions OK (dialout group)"
    return
  fi
  warn "Your user is not in the 'dialout' group."
  warn "Without that, flashing may fail with a permission error."
  if [[ -t 0 ]]; then
    read -r -p "Add yourself to dialout now? (may ask for sudo) [y/N] " ans
    if [[ "${ans,,}" == "y" || "${ans,,}" == "yes" ]]; then
      sudo usermod -aG dialout "$USER" || die "Could not add user to dialout"
      warn "Done. Log out or reboot, then run this script again."
      exit 0
    fi
  else
    warn "Run: sudo usermod -aG dialout \$USER && then log out"
  fi
}

detect_port() {
  if [[ -n "$UPLOAD_PORT" ]]; then
    [[ -e "$UPLOAD_PORT" ]] || die "Port does not exist: $UPLOAD_PORT"
    echo "$UPLOAD_PORT"
    return
  fi

  local candidates=()
  local p
  for p in /dev/ttyUSB* /dev/ttyACM* /dev/cu.usbserial* /dev/cu.usbmodem* /dev/cu.wchusbserial*; do
    [[ -e "$p" ]] || continue
    candidates+=("$p")
  done

  if [[ ${#candidates[@]} -eq 0 ]]; then
    die "No USB board detected.
1) Connect the ESP32 using the programming USB port (UART / COM / CH343).
2) Use a data-capable cable.
3) Retry, or pass the port: ./scripts/setup_and_flash.sh --port /dev/ttyUSB0"
  fi

  if [[ ${#candidates[@]} -eq 1 ]]; then
    echo "${candidates[0]}"
    return
  fi

  warn "Multiple USB ports detected:"
  local i=1
  for p in "${candidates[@]}"; do
    printf '  %s) %s\n' "$i" "$p"
    i=$((i + 1))
  done
  if [[ -t 0 ]]; then
    read -r -p "Choose the PROGRAMMING port number (not OTG): " idx
    [[ "$idx" =~ ^[0-9]+$ ]] || die "Invalid selection"
    [[ "$idx" -ge 1 && "$idx" -le ${#candidates[@]} ]] || die "Selection out of range"
    echo "${candidates[$((idx - 1))]}"
  else
    echo "${candidates[0]}"
  fi
}

install_platformio
check_serial_permission
export PATH="$HOME/.platformio/penv/bin:$HOME/.local/bin:$PATH"

say "Downloading toolchains and building firmware..."
pio_cmd run

if [[ "$BUILD_ONLY" -eq 1 ]]; then
  ok "Build OK (flash skipped)."
  exit 0
fi

PORT="$(detect_port)"
ok "Selected port: $PORT"
warn "Important: flash via the programming USB (UART/CH343), NOT the OTG USB."

say "Flashing FosiFix..."
pio_cmd run -t upload --upload-port "$PORT"

ok ""
ok "=== Flash complete ==="
ok ""
cat <<EOF
Next steps (order matters):

1) On your phone or PC, find WiFi network:  ${BOLD}FosiFix Setup${NC}
2) Join it (no password).
3) Open a browser at:  ${BOLD}http://192.168.4.1${NC}
4) Pick your home WiFi, enter the password, and save.
5) Switch back to your normal WiFi.
6) Open:  ${BOLD}http://fosifix.local${NC}
7) Connect the ESP32 OTG USB to the MC331 USB-C port.
8) Power on the amp. The Fix applies automatically within a few seconds.

If fosifix.local does not open, check the IP in your router or serial monitor logs.
EOF

if [[ "$DO_MONITOR" -eq 1 ]]; then
  say "Opening serial monitor (Ctrl+C to exit)..."
  pio_cmd device monitor --port "$PORT" --baud 115200
fi
