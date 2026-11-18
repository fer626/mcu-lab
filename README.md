# mcu-lab

[English](#english) · [Español](#español)

---

## English

A minimal ESP-IDF starter with **ESP-NOW authenticated encryption**: the
ESP32-S3 hub shows on a 128x64 OLED the nodes that answer, but only if the
message passes an AES-128-GCM tag check and a sequence-number check.

### What it does

- Prints chip info and MAC address on boot.
- S3 hub: broadcasts a public `hola` every 10 s (trigger only, no secret) and
  renders on the OLED the name + message of each node that answers **with a
  valid tag**.
- C3 node: replies `"<received> mundo"` encrypted with its role key (the test
  node uses the `TINACO` key), with `src`, `dst`, `seq` and the header as AAD.
- Replay is rejected: a repeated `seq` is dropped before decryption.
- Builds one firmware per target from a single source tree via GitHub Actions.

### Security model

| Layer | Mechanism | State in this demo |
|-------|-----------|--------------------|
| Link | ESP-NOW PMK + per-peer LMK (AES-CCM) | Auto only when the 3 MACs are filled in `secrets.h`; otherwise off (test mode) |
| App | AES-128-GCM, 16-byte tag, AAD over header, nonce `src+seq+random` | Always on |
| Replay | `seq` per node, stored in NVS on the C3 | Always on |

The test node replies to **any hub MAC** it hears, so you do not need to know
MAC addresses to try it. Anyone can hear the public `hola`, but nobody without
the right key can forge a reply or read the payload.

### Keys (important)

- `main/secrets.h` is **gitignored** and holds the real keys.
- If it is missing (e.g. in CI), `main/secrets_dev.h` is used: it has fixed,
  **public** keys — good only to check that the build works, not for privacy.
- To get real keys in the CI binaries, add these **GitHub repository secrets**
  (Settings → Secrets → Actions) and re-run the workflow:
  `ESPNOW_PMK`, `K_TINACO`, `K_CISTERNA`, `K_BOMBA`, `LMK_TINACO`,
  `LMK_CISTERNA`, `LMK_BOMBA`. Each is 32 hex characters (`python3 -c
  "import secrets; print(secrets.token_hex(16))"`).
- Rotating keys means flashing every board again.

### Wiring (OLED on the ESP32-S3)

| OLED | ESP32-S3-DevKitC-1 |
|------|--------------------|
| VCC  | 3V3                |
| GND  | GND                |
| SDA  | GPIO8              |
| SCL  | GPIO9              |

Address `0x3C`. Pins, channel and power live in [`main/board.h`](main/board.h).
`TX_POWER_QDBM=34` (8.5 dBm) is for the bench; use `TX_POWER_QDBM_ROOF=78`
(20 dBm) for the roof tinaco.

### Verifying

1. Flash `out-a` (S3) and `out-b` (C3) from the Actions artifacts.
2. On the S3 console you should see `AUTH OK <- c3-XXXX ...`; the OLED shows
   `c3-XXXX` / `hola mundo`.
3. Change one byte of `K_TINACO` in the hub and rebuild: you get `AUTH FAILED`
   and the OLED stays empty.
4. Replay an old frame: `replay seq=... (last=...)` is logged and discarded.
5. A sniffer sees only random bytes for the reply (the `hola` is public).

### Layout

```
main/
├── board.h         # pins, channel, power
├── main.c          # hub (S3) + node (C3)
├── ssd1306.c/.h    # minimal OLED driver + 5x7 font
├── protocol.h      # secure message structs
├── crypto.c/.h     # AES-128-GCM seal/open
├── secrets.h       # REAL KEYS - gitignored
└── secrets_dev.h   # public dev keys (CI fallback)
```

### License

MIT. See [LICENSE](LICENSE).

---

## Español

Plantilla ESP-IDF con **cifrado autenticado ESP-NOW**: el hub S3 muestra en una
OLED de 128x64 los nodos que responden, pero solo si el mensaje pasa la firma
AES-128-GCM y la comprobación de secuencia.

### Qué hace

- Muestra info del chip y la MAC al arrancar.
- Hub S3: difunde un `hola` público cada 10 s (solo disparador) y muestra en la
  OLED el nombre + mensaje de cada nodo que responde **con tag válido**.
- Nodo C3: responde `"<recibido> mundo"` cifrado con la clave de su rol (el
  nodo de prueba usa la clave `TINACO`), con `src`, `dst`, `seq` y la cabecera
  como AAD.
- Los repetidos se descartan: un `seq` ya visto se ignora antes de descifrar.
- Compila un firmware por target desde un único árbol con GitHub Actions.

### Modelo de seguridad

| Capa | Mecanismo | Estado en este demo |
|------|-----------|---------------------|
| Enlace | ESP-NOW PMK + LMK por par (AES-CCM) | Solo si las 3 MACs están en `secrets.h`; si no, apagado (modo prueba) |
| App | AES-128-GCM, tag de 16 B, AAD sobre cabecera, nonce `src+seq+azar` | Siempre activo |
| Replay | `seq` por nodo, guardado en NVS en el C3 | Siempre activo |

El nodo de prueba responde a **cualquier MAC de hub** que escuche, así que no
necesitas saber las MACs para probarlo. Cualquiera puede oír el `hola`, pero
sin la clave nadie puede falsificar una respuesta ni leer el contenido.

### Claves (importante)

- `main/secrets.h` está **en .gitignore** y contiene las claves reales.
- Si falta (por ejemplo en CI), se usa `main/secrets_dev.h`: claves fijas y
  **públicas**, solo para comprobar que compila, no para privacidad.
- Para que los binarios de CI tengan claves reales, añade estos ** Secrets del
  repositorio** (Settings → Secrets → Actions) y vuelve a ejecutar el workflow:
  `ESPNOW_PMK`, `K_TINACO`, `K_CISTERNA`, `K_BOMBA`, `LMK_TINACO`,
  `LMK_CISTERNA`, `LMK_BOMBA`. Cada uno son 32 caracteres hex (`python3 -c
  "import secrets; print(secrets.token_hex(16))"`).
- Rotar claves implica reflashear todas las placas.

### Conexión (OLED en la ESP32-S3)

| OLED | ESP32-S3-DevKitC-1 |
|------|--------------------|
| VCC  | 3V3                |
| GND  | GND                |
| SDA  | GPIO8              |
| SCL  | GPIO9              |

Dirección `0x3C`. Pines, canal y potencia en [`main/board.h`](main/board.h).
`TX_POWER_QDBM=34` (8.5 dBm) es para banco; usa `TX_POWER_QDBM_ROOF=78`
(20 dBm) para el tinaco del techo.

### Verificación

1. Flashea `out-a` (S3) y `out-b` (C3) desde los artefactos de Actions.
2. En la consola del S3 debe aparecer `AUTH OK <- c3-XXXX ...`; la OLED muestra
   `c3-XXXX` / `hola mundo`.
3. Cambia 1 byte de `K_TINACO` en el hub y recompila: sale `AUTH FAILED` y la
   OLED se queda vacía.
4. Repite un paquete viejo: se registra `replay seq=... (last=...)` y se descarta.
5. Un sniffer solo ve bytes aleatorios en la respuesta (el `hola` es público).

### Estructura

```
main/
├── board.h         # pines, canal, potencia
├── main.c          # hub (S3) + nodo (C3)
├── ssd1306.c/.h    # driver OLED mínimo + fuente 5x7
├── protocol.h      # structs del mensaje seguro
├── crypto.c/.h     # AES-128-GCM seal/open
├── secrets.h       # CLAVES REALES - ignorado por git
└── secrets_dev.h   # claves públicas de desarrollo (fallback CI)
```

### Licencia

MIT. Ver [LICENSE](LICENSE).
