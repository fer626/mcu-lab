# mcu-lab

[English](#english) · [Español](#español)

---

## English

A minimal, ready-to-fork **ESP-IDF** starter. Build the firmware in the cloud
with **GitHub Actions** and flash the resulting binary from any PC or Android
phone — no local toolchain required.

### What it does

- Prints chip information and the MAC address on boot.
- Emits a heartbeat (`tick`) every 2 seconds on the serial console.
- Builds one firmware per target from a single source tree.

### Layout

```
.
├── CMakeLists.txt
├── sdkconfig.defaults
├── main/
│   ├── CMakeLists.txt
│   └── main.c
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
- Emite un `tick` cada 2 segundos por la consola serie.
- Compila un firmware por *target* desde un único árbol de código.

### Estructura

```
.
├── CMakeLists.txt
├── sdkconfig.defaults
├── main/
│   ├── CMakeLists.txt
│   └── main.c
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
