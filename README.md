# mcu-lab

[English](#english) · [Español](#español)

---

## English

A minimal, ready-to-fork **ESP-IDF** starter. Build the firmware in the cloud
with **GitHub Actions** and flash the resulting binary from any PC or Android
phone — no local toolchain required.

### What it does

- Prints chip information and the MAC address on boot.
- ESP32-S3 acts as an **ESP-NOW hub**: broadcasts `hola` every 10 seconds and
  shows, on a 128x64 I2C OLED, the name of every node that answers together
  with the message it sent.
- ESP32-C3 acts as a **node**: replies `<message> mundo` tagged with its name.
  The name defaults to `c3-XXXX` (last two bytes of the STA MAC) and can be
  changed from the serial console.
- Builds one firmware per target from a single source tree.

### Wiring (OLED on the ESP32-S3)

The hub drives an **SSD1306/SH1106 128x64 I2C OLED**:

| OLED | ESP32-S3-DevKitC-1 |
|------|--------------------|
| VCC  | 3V3                |
| GND  | GND                |
| SDA  | GPIO8              |
| SCL  | GPIO9              |

I2C address `0x3C` (some modules use `0x3D`). Pins, address, panel size and the
broadcast period live in [`main/board.h`](main/board.h) — change them there if
your wiring differs. If no panel answers on the bus, the hub keeps working and
just logs to the console. The display shows up to four nodes (most recent
first); each line fits 21 characters.

### Naming a node

Every ESP32-C3 shows up as `c3-XXXX`, where `XXXX` are the last two bytes of its
MAC, so boards are distinguishable with no setup. To give one a friendly name,
open its serial console (115200 baud) and type:

```
name cocina
```

The name is saved in NVS and survives reboots. `name` with no arguments prints
the current name; `name -` restores the MAC default. On the ESP32-C3 the console
is the built-in USB-Serial-JTAG port (see `sdkconfig.defaults.esp32c3`), so you
can use the same USB cable you flash with.

### Heat and range

ESP-NOW keeps the radio listening, so the node's receiver stays on and the
module will feel warm. This demo keeps that cost low by broadcasting every 10 s
(`SEND_PERIOD_MS`) and lowering TX power to 8.5 dBm (`TX_POWER_QDBM`). Increase
`SEND_PERIOD_MS` to send even less often; raise `TX_POWER_QDBM` (up to ~78 =
20 dBm) if you need more range. Both live in [`main/board.h`](main/board.h).

### Layout

```
.
├── CMakeLists.txt
├── sdkconfig.defaults
├── sdkconfig.defaults.esp32c3
├── main/
│   ├── CMakeLists.txt
│   ├── board.h        # pin map and tunables
│   ├── main.c         # hub (S3) and node (C3) roles
│   ├── ssd1306.c      # tiny SSD1306 I2C driver + 5x7 font
│   └── ssd1306.h
└── .github/workflows/build.yml
```

### Quick start

1. **Fork** this repository (or clone it).
2. Edit `main/main.c` — or add your own components under `main/` or `components/`.
3. **Push to `main`.** The workflow builds every target automatically.
4. Open the **Actions** tab, pick the latest run, and download the artifact you need.
5. **Flash** `merged-flash.bin` at offset `0x0`.

   With [esptool](https://github.com/espressif/esptool):
   ```bash
   esptool.py --chip esp32s3 write_flash 0x0 merged-flash.bin
   ```
   From Android (Termux) with `nrflash`:
   ```bash
   nrflash write --offset 0x0 merged-flash.bin
   ```

6. **Watch the logs** at `115200` baud (the default build uses the USB-Serial-JTAG console).

### Targets

The matrix in `.github/workflows/build.yml` decides what gets built:

| Artifact | Target  |
|----------|---------|
| `out-a`  | esp32s3 |
| `out-b`  | esp32c3 |

Add rows to the matrix to build for more chips.

### Building locally (optional)

CI already does this for you, but if you want to build on your machine:

```bash
. $IDF_PATH/export.sh
idf.py set-target esp32s3
idf.py build
```

### License

MIT. See [LICENSE](LICENSE).

---

## Español

Plantilla mínima de **ESP-IDF** lista para hacer *fork*. Compila el firmware en
la nube con **GitHub Actions** y flashea el binario desde cualquier PC o teléfono
Android, sin instalar el *toolchain* en local.

### Qué hace

- Muestra la información del chip y la dirección MAC al arrancar.
- La ESP32-S3 actúa como **hub ESP-NOW**: difunde `hola` cada 10 segundos y
  muestra en una OLED I2C de 128x64 el nombre de cada nodo que responde junto
  con el mensaje que envió.
- La ESP32-C3 actúa como **nodo**: responde `<mensaje> mundo` etiquetado con su
  nombre. El nombre por defecto es `c3-XXXX` (los dos últimos bytes de la MAC
  STA) y se puede cambiar por la consola serie.
- Compila un firmware por *target* desde un único árbol de código.

### Conexión (OLED en la ESP32-S3)

El hub controla una **OLED SSD1306/SH1106 de 128x64 por I2C**:

| OLED | ESP32-S3-DevKitC-1 |
|------|--------------------|
| VCC  | 3V3                |
| GND  | GND                |
| SDA  | GPIO8              |
| SCL  | GPIO9              |

Dirección I2C `0x3C` (algunos módulos usan `0x3D`). Los pines, la dirección, el
tamaño del panel y el periodo de difusión están en
[`main/board.h`](main/board.h) — cámbialos ahí si tu cableado es distinto. Si no
hay panel en el bus, el hub sigue funcionando y solo escribe en la consola. La
pantalla muestra hasta cuatro nodos (el más reciente primero); cada línea cabe
en 21 caracteres.

### Ponerle nombre a un nodo

Cada ESP32-C3 aparece como `c3-XXXX`, donde `XXXX` son los dos últimos bytes de
su MAC, así que se distinguen sin configurar nada. Para darle un nombre humano,
abre su consola serie (115200 baudios) y escribe:

```
name cocina
```

El nombre se guarda en NVS y sobrevive a los reinicios. `name` sin argumentos
muestra el nombre actual; `name -` restaura el valor por defecto de la MAC. En
la ESP32-C3 la consola es el puerto USB-Serial-JTAG integrado (ver
`sdkconfig.defaults.esp32c3`), así que puedes usar el mismo cable USB con el que
flasheas.

### Calor y alcance

ESP-NOW mantiene la radio escuchando, así que el receptor del nodo queda
encendido y el módulo se nota caliente. Este ejemplo lo reduce difundiendo cada
10 s (`SEND_PERIOD_MS`) y bajando la potencia de TX a 8.5 dBm (`TX_POWER_QDBM`).
Sube `SEND_PERIOD_MS` para enviar aún menos; sube `TX_POWER_QDBM` (hasta ~78 =
20 dBm) si necesitas más alcance. Ambos están en
[`main/board.h`](main/board.h).

### Estructura

```
.
├── CMakeLists.txt
├── sdkconfig.defaults
├── sdkconfig.defaults.esp32c3
├── main/
│   ├── CMakeLists.txt
│   ├── board.h        # mapa de pines y ajustes
│   ├── main.c         # roles hub (S3) y nodo (C3)
│   ├── ssd1306.c      # driver mínimo SSD1306 por I2C + fuente 5x7
│   └── ssd1306.h
└── .github/workflows/build.yml
```

### Inicio rápido

1. Haz **fork** del repositorio (o clónalo).
2. Edita `main/main.c` — o añade tus propios componentes en `main/` o `components/`.
3. **Sube los cambios a `main`.** El workflow compila cada *target* automáticamente.
4. Abre la pestaña **Actions**, entra al último *run* y descarga el artefacto que necesites.
5. **Flashea** `merged-flash.bin` en el offset `0x0`.

   Con [esptool](https://github.com/espressif/esptool):
   ```bash
   esptool.py --chip esp32s3 write_flash 0x0 merged-flash.bin
   ```
   Desde Android (Termux) con `nrflash`:
   ```bash
   nrflash write --offset 0x0 merged-flash.bin
   ```

6. **Mira los logs** a `115200` baudios (el build por defecto usa la consola USB-Serial-JTAG).

### Targets

La matriz en `.github/workflows/build.yml` decide qué se compila:

| Artefacto | Target  |
|-----------|---------|
| `out-a`   | esp32s3 |
| `out-b`   | esp32c3 |

Añade filas a la matriz para compilar para más chips.

### Compilar en local (opcional)

CI ya lo hace por ti, pero si quieres compilar en tu máquina:

```bash
. $IDF_PATH/export.sh
idf.py set-target esp32s3
idf.py build
```

### Licencia

MIT. Ver [LICENSE](LICENSE).
