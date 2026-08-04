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
FosiFix — setup y flasheo automático

Uso:
  ./scripts/setup_and_flash.sh              # instala deps, compila y flashea
  ./scripts/setup_and_flash.sh --build-only # solo compila
  ./scripts/setup_and_flash.sh --port /dev/ttyUSB0
  ./scripts/setup_and_flash.sh --monitor    # flashea y abre el monitor serie

Requisitos mínimos:
  - Linux o macOS
  - Python 3
  - Cable USB de datos (no solo carga)
  - Placa ESP32-S3 N16R8 (o compatible)
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
      [[ $# -ge 2 ]] || die "Falta el valor de --port"
      UPLOAD_PORT="$2"
      shift 2
      ;;
    *) die "Opción desconocida: $1 (usa --help)" ;;
  esac
done

say "${BOLD}=== FosiFix setup ===${NC}"
say "Proyecto: $ROOT"

need_cmd() {
  command -v "$1" >/dev/null 2>&1
}

install_platformio() {
  export PATH="$HOME/.platformio/penv/bin:$HOME/.local/bin:$PATH"

  if need_cmd pio; then
    ok "PlatformIO CLI encontrado: $(command -v pio)"
    return
  fi
  if need_cmd platformio; then
    ok "PlatformIO CLI encontrado: $(command -v platformio)"
    return
  fi

  warn "PlatformIO no está instalado. Intentando instalarlo..."
  if ! need_cmd python3; then
    die "Necesitás Python 3. Instalólo y volvé a ejecutar este script."
  fi

  if need_cmd curl; then
    curl -fsSL https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py -o /tmp/get-platformio.py
    python3 /tmp/get-platformio.py || \
      python3 -m pip install --user -U platformio || \
      die "No se pudo instalar PlatformIO. Ver https://platformio.org/install/cli"
  else
    python3 -m pip install --user -U platformio || \
      die "No se pudo instalar PlatformIO con pip. Instalalo desde https://platformio.org/install/cli"
  fi

  export PATH="$HOME/.platformio/penv/bin:$HOME/.local/bin:$PATH"
  if need_cmd pio || need_cmd platformio; then
    ok "PlatformIO instalado"
  else
    die "PlatformIO se instaló pero no está en el PATH. Abrí una terminal nueva y reintentá."
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
    ok "Permisos serie OK (grupo dialout)"
    return
  fi
  warn "Tu usuario no está en el grupo 'dialout'."
  warn "Sin eso, el flasheo puede fallar por permisos."
  if [[ -t 0 ]]; then
    read -r -p "¿Agregar a dialout ahora? (puede pedir sudo) [s/N] " ans
    if [[ "${ans,,}" == "s" || "${ans,,}" == "si" || "${ans,,}" == "sí" || "${ans,,}" == "y" ]]; then
      sudo usermod -aG dialout "$USER" || die "No se pudo agregar al grupo dialout"
      warn "Listo. Cerrá sesión o reiniciá y volvé a ejecutar este script."
      exit 0
    fi
  else
    warn "Ejecutá: sudo usermod -aG dialout \$USER && luego cerrá sesión"
  fi
}

detect_port() {
  if [[ -n "$UPLOAD_PORT" ]]; then
    [[ -e "$UPLOAD_PORT" ]] || die "No existe el puerto: $UPLOAD_PORT"
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
    die "No se detectó ninguna placa USB.
1) Conectá el ESP32 por el puerto USB de programación (UART / COM / CH343).
2) Usá un cable con datos.
3) Reintentá, o pasá el puerto: ./scripts/setup_and_flash.sh --port /dev/ttyUSB0"
  fi

  if [[ ${#candidates[@]} -eq 1 ]]; then
    echo "${candidates[0]}"
    return
  fi

  warn "Se detectaron varios puertos USB:"
  local i=1
  for p in "${candidates[@]}"; do
    printf '  %s) %s\n' "$i" "$p"
    i=$((i + 1))
  done
  if [[ -t 0 ]]; then
    read -r -p "Elegí el número del puerto de PROGRAMACIÓN (no el OTG): " idx
    [[ "$idx" =~ ^[0-9]+$ ]] || die "Selección inválida"
    [[ "$idx" -ge 1 && "$idx" -le ${#candidates[@]} ]] || die "Selección fuera de rango"
    echo "${candidates[$((idx - 1))]}"
  else
    echo "${candidates[0]}"
  fi
}

install_platformio
check_serial_permission
export PATH="$HOME/.platformio/penv/bin:$HOME/.local/bin:$PATH"

say "Descargando herramientas y compilando firmware..."
pio_cmd run

if [[ "$BUILD_ONLY" -eq 1 ]]; then
  ok "Compilación OK (sin flashear)."
  exit 0
fi

PORT="$(detect_port)"
ok "Puerto seleccionado: $PORT"
warn "Importante: flasheá por el USB de programación (UART/CH343), NO por el USB OTG."

say "Flasheando FosiFix..."
pio_cmd run -t upload --upload-port "$PORT"

ok ""
ok "=== Flasheo completado ==="
ok ""
cat <<EOF
Próximos pasos (muy importante el orden):

1) En tu celular o PC, buscá la red WiFi:  ${BOLD}FosiFix Setup${NC}
2) Conectate a esa red (sin contraseña).
3) Abrí el navegador en:  ${BOLD}http://192.168.4.1${NC}
4) Elegí tu WiFi de casa, escribí la contraseña y guardá.
5) Volvé a tu WiFi normal.
6) Abrí:  ${BOLD}http://fosifix.local${NC}
7) Conectá el USB OTG del ESP32 al puerto USB-C del MC331.
8) Encendé el amplificador. En unos segundos se aplica el Fix solo.

Si fosifix.local no abre, mirá la IP en el router o en los logs del monitor serie.
EOF

if [[ "$DO_MONITOR" -eq 1 ]]; then
  say "Abriendo monitor serie (Ctrl+C para salir)..."
  pio_cmd device monitor --port "$PORT" --baud 115200
fi
