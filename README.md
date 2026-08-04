# FosiFix

**FosiFix** es un pequeño dispositivo basado en **ESP32-S3** que se conecta al amplificador **Fosi Audio MC331** y corrige automáticamente el corte de audio a bajo volumen (noise gate agresivo del DSP).

Cuando encendés el amplificador, su electrónica interna vuelve a poner el “noise gate” en valores de fábrica. FosiFix detecta el amp por USB y vuelve a aplicar el ajuste de la comunidad (~**-90 dB**), una y otra vez, sin que tengas que usar una PC.

> Proyecto open source, independiente. No está afiliado a Fosi Audio.

---

## ¿Qué necesitás?

| Elemento | Detalle |
|----------|---------|
| Placa | **ESP32-S3** con flash grande (recomendado **N16R8**: 16 MB flash + 8 MB PSRAM) y **USB OTG** |
| Cables | 1) Cable USB de **datos** a la PC (puerto de programación) · 2) Cable USB-C al **MC331** (puerto OTG del ESP32) |
| PC | Linux o macOS (Windows: usá [PlatformIO IDE](https://platformio.org/platformio-ide)) |
| Amplificador | Fosi Audio **MC331** |

### Importante: dos puertos USB en la placa

Muchas placas ESP32-S3 tienen **dos** conectores USB:

| Puerto | Para qué sirve |
|--------|----------------|
| **UART / COM / Serial** (a menudo con chip CH340/CH343) | **Programar** FosiFix y ver logs |
| **USB / OTG** | Conectar al **MC331** (modo Host) |

Si flasheás por el puerto OTG, suele fallar o quedar en silencio. Usá siempre el de **programación**.

---

## Instalación rápida (recomendado)

En Linux o macOS, desde la carpeta del proyecto:

```bash
chmod +x scripts/setup_and_flash.sh
./scripts/setup_and_flash.sh
```

El script intenta:

1. Instalar **PlatformIO** si hace falta  
2. Avisarte por permisos USB (`dialout` en Linux)  
3. Compilar el firmware  
4. Detectar el puerto serie y flashear  
5. Explicarte los pasos de WiFi  

Opciones útiles:

```bash
./scripts/setup_and_flash.sh --build-only
./scripts/setup_and_flash.sh --port /dev/ttyUSB0
./scripts/setup_and_flash.sh --monitor
./scripts/setup_and_flash.sh --help
```

---

## Guía paso a paso (sin ser técnico)

### 1. Descargar el proyecto

- Opción A: cloná el repositorio con Git  
- Opción B: en GitHub, **Code → Download ZIP** y descomprimilo

### 2. Conectar el ESP32 a la PC

1. Usá un cable USB **con datos** (algunos de carga no sirven).  
2. Conectalo al puerto de **programación** del ESP32 (UART/COM), no al OTG.  
3. En Linux, si el flasheo dice “Permission denied”:

```bash
sudo usermod -aG dialout $USER
```

Luego **cerrá sesión** (o reiniciá) y volvé a intentar.

### 3. Flashear FosiFix

```bash
cd fosi-fix-mc331-sp32
chmod +x scripts/setup_and_flash.sh
./scripts/setup_and_flash.sh
```

Esperá a que diga que el flasheo terminó.

### 4. Configurar el WiFi (primera vez)

1. En el celular o PC, buscá la red WiFi **`FosiFix Setup`**.  
2. Conectate (no pide contraseña).  
3. Abrí el navegador en **`http://192.168.4.1`**.  
4. Elegí tu WiFi de casa, escribí la contraseña y guardá.  
5. El ESP32 se reinicia y se une a tu red.  
6. Volvé a tu WiFi normal.  
7. Abrí **`http://fosifix.local`**.

Si `fosifix.local` no abre (pasa a veces en Android o routers viejos), mirá en el router la IP del dispositivo llamado `fosifix` y abrila en el navegador (`http://192.168.x.x`).

### 5. Conectar al amplificador

1. Conectá el puerto **USB OTG** del ESP32 al **USB-C del MC331**.  
2. Encendé el amplificador.  
3. Esperá unos segundos: FosiFix aplica el Fix solo.  
4. En la web deberías ver que el MC331 está conectado y el Fix aplicado.

Listo: cada vez que el amp se reinicie o se reconecte, FosiFix vuelve a aplicar el ajuste.

---

## Uso diario

- Dejá el ESP32 alimentado y conectado al MC331.  
- Abrí `http://fosifix.local` si querés ver el estado o pulsar **Aplicar nuevamente**.  
- El modo automático reenvía el Fix periódicamente (por defecto cada 30 s) por si el amp vuelve a los valores de fábrica.

---

## Compilar a mano (avanzado)

Con [PlatformIO](https://platformio.org/) instalado:

```bash
pio run
pio run -t upload --upload-port /dev/ttyUSB0
pio device monitor --port /dev/ttyUSB0
```

La interfaz web se **embebe en el firmware** al compilar (no hace falta `uploadfs`).

---

## Qué hace el firmware

- Detecta el MC331 por USB (`VID 0x8888`, `PID 0x1717` o `0x171E`)  
- Espera a que el DSP termine de arrancar  
- Envía el paquete HID del Noise Suppressor (`0x88`) con umbral **~-90 dB**  
- Reaplica el Fix al reconectar y de forma periódica  
- Expone WiFi + panel web + OTA + logs

---

## API (referencia)

| Método | Ruta | Descripción |
|--------|------|-------------|
| `GET` | `/api/status` | Estado USB, WiFi, memoria |
| `GET` | `/api/logs` | Últimos logs |
| `POST` | `/api/fix` | Pedir aplicar el Fix |
| `POST` | `/api/reboot` | Reiniciar |
| `GET`/`POST` | `/api/settings` | Configuración |
| `GET` | `/api/wifi/scan` | Escanear redes |
| `POST` | `/api/ota` | Actualizar firmware por red |

WebSocket en el puerto **81** (`status` + `log`).

---

## Hardware compatible

- Placa: ESP32-S3-WROOM-1 **N16R8** (u otra S3 con OTG + suficiente flash)  
- Amplificador: Fosi Audio MC331  
- Alimentación: la de la placa / USB de programación; el OTG habla con el amp

---

## Problemas frecuentes

**No aparece ninguna placa al flashear**  
Cable sin datos, puerto OTG en vez de UART, o faltan permisos `dialout`.

**La web no carga / pantalla rara**  
Flasheá de nuevo con el script (la UI va dentro del firmware). Probá forzar recarga del navegador.

**`fosifix.local` no funciona**  
Usá la IP del router. En algunos celulares mDNS no anda.

**El Fix no se aplica**  
Confirmá cable OTG → MC331, amp encendido, y en la web que diga MC331 conectado. Probá **Aplicar nuevamente**.

**Monitor serie en blanco**  
Estás mirando el puerto OTG. Abrí el monitor en el puerto UART/COM.

---

## Estructura del proyecto

```
src/           firmware (USB Host, WiFi, web, settings)
data/          interfaz web (se embebe al compilar)
scripts/       setup_and_flash.sh + embed_ui.py
platformio.ini configuración PlatformIO
```

---

## Licencia

MIT — ver [LICENSE](LICENSE).

## Créditos

- Investigación comunitaria del Low Volume Cut-Off Fix (foro Fosi Audio / ACP Workbench)  
- Espressif USB Host Library  
- Contribuidores de FosiFix

## Aviso

Usá este proyecto bajo tu propio riesgo. No modifica el firmware interno del MC331 de forma permanente: el ajuste se reaplica por USB mientras FosiFix esté conectado.
