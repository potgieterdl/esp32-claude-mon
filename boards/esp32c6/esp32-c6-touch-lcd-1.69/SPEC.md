# Waveshare ESP32-C6-Touch-LCD-1.69
> **Status:** ✅ supported (reference device) · **Arch:** esp32c6 · **Vendor:** Waveshare
> Product: https://www.waveshare.com/wiki/ESP32-C6-Touch-LCD-1.69 · Schematic: [`docs/ESP32-C6-Touch-LCD-1.69-Schematic.pdf`](../../../docs/ESP32-C6-Touch-LCD-1.69-Schematic.pdf)

Canonical hardware truth for this board. **Single source of truth — do not duplicate these facts in other
docs; link here instead.** App structure & render path → [`docs/ARCHITECTURE.md`](../../../docs/ARCHITECTURE.md).

## At a glance
- **MCU:** ESP32-C6 — single-core 32-bit RISC-V @ 160 MHz, Wi-Fi 6, BLE 5, 802.15.4. **No PSRAM.**
- **Flash:** 8 MB. **Display:** 1.69″ IPS 240×280, driver **ST7789V2** (SPI). **Touch:** **CST816T** (I²C).
- **Other:** QMI8658 6-axis IMU · PCF85063 RTC · ES8311 audio codec + NS4150B amp + mic.
- **Power:** USB-C; ETA6098 Li-charger; battery voltage via ADC (gated by BAT_EN).
- **USB:** native USB-Serial/JTAG (no UART bridge). Enumerates as `VID 303A : PID 1001`.
- **Device-adapter contract implemented:** display ✅ · touch ✅ · backlight ✅ · battery ✅ · audio ✅ · RTC ✅.

## Pinout (CANONICAL)
| Signal | GPIO | | Signal | GPIO |
|---|---|---|---|---|
| LCD SCK | **1** | | I²C SDA (shared) | **8** |
| LCD MOSI / DIN | **2** | | I²C SCL (shared) | **7** |
| LCD DC | **3** | | Touch INT (TP_INT) | **11** |
| LCD RST | **4** | | BAT_ADC | **0** |
| LCD CS | **5** | | BAT_EN (HIGH to read battery) | **15** |
| LCD BL (backlight) | **6** | | Button 1 = BOOT | **9** |
| | | | Button 2 | **18** |

**Shared I²C bus (SDA=8, SCL=7):** CST816T touch `0x15`, QMI8658 IMU `0x6B`, PCF85063 RTC `0x51`,
ES8311 codec `0x18`. **Audio I²S:** MCLK=19, BCLK=20, LRCK=22, DOUT=23, DIN=21.

## Display / touch specifics
- **Row offset 20** — the 240×280 panel is a 240×320 controller windowed; without it the image is shifted.
- **`rgb_order = true`** (RGB) for correct colors; **`invert = true`** (IPS needs INVON).
- **SPI 80 MHz is safe here** because the bus is **write-only** (`miso = -1`): SCK/MOSI on GPIO1/2 route
  through the GPIO matrix whose ~40 MHz cap is a *MISO-read* constraint, which doesn't apply to a write-only bus.
- Backlight **active-high on GPIO6**, driven by LEDC PWM (`ledcAttach(6,5000,8)`).
- Touch is portrait-native (0..239 × 0..279); the app maps it to landscape (x↔y flip).
- **Read touch INT-gated** (`TP_INT` = GPIO11, FALLING): only touch the I2C bus when the CST816 signals a
  touch event, then poll until the finger lifts. Blind ~30 Hz polling of the *idle* controller floods the log
  with `[259] ESP_ERR_INVALID_STATE` (~1/s); INT-gating drops it to zero. See `firmware/src/main.cpp`
  `touch_read()` / `touch_isr()` (issue #18).

LovyanGFX bus/panel config lives in `firmware/src/main.cpp` (`class LGFX`). Why LovyanGFX + 80 MHz (not
Arduino_GFX/TFT_eSPI) → [`docs/ARCHITECTURE.md`](../../../docs/ARCHITECTURE.md#render-path).

## Board quirks & gotchas (learned the hard way)
- **Use a DATA USB-C cable**, not charge-only. Charge-only symptom: screen powers on but no COM port / no `303A` device.
- Native USB COM port **drifts/drops after an esptool reset** → `Could not open COMx`. Replug and retry,
  **or flash over WiFi (OTA)** — see *Flashing* below. The reset-toggle re-enumerates the on-chip USB mid-flash.
- **HWCDC serial is unreliable on the C6** — `Serial.print` often shows nothing. Confirm "is it alive?" via
  the **screen/backlight**, never serial.
- Download/bootloader mode (if a normal flash won't connect): hold **BOOT**, tap **RESET**, release BOOT.

## Build / toolchain
- Platform: **pioarduino fork** pinned `55.03.38-1` (official `platformio/espressif32` lagged on C6) →
  Arduino-ESP32 3.3.8, IDF 5.5. Board id `esp32-c6-devkitc-1`, env `esp32-c6`, framework `arduino`.
- Required USB-serial flags: `-DARDUINO_USB_CDC_ON_BOOT=1 -DARDUINO_USB_MODE=1`.
- Partitions: `default_8MB.csv` (two ~3.3 MB OTA app slots + `spiffs`/LittleFS). Full config: [`firmware/platformio.ini`](../../../firmware/platformio.ini) (don't copy flags here — link).

## Flashing & recovery
- **USB:** `pio run -d firmware -t upload` (set `$env:PYTHONIOENCODING='utf-8'` first on Windows).
- **OTA (no cable, recommended when USB drifts):** see [`CLAUDE.md`](../../../CLAUDE.md) → *Flash over WiFi*.
- **Restore shipped demo:** flash `vendor/.../Firmware/01_factory.bin` at 0x0.
- **Rollback:** a known-good image from [`firmware/releases/`](../../../firmware/releases/README.md).

## References
- Upstream (pull first): https://github.com/waveshareteam/ESP32-C6-Touch-LCD-1.69 → `vendor/ESP32-C6-Touch-LCD-1.69`.
- Wiki (blocks bots; read via `https://r.jina.ai/<url>`): https://www.waveshare.com/wiki/ESP32-C6-Touch-LCD-1.69
- Vendor examples + prebuilt `Firmware/*.bin` under `vendor/.../Examples/{Arduino,ESP-IDF}`.
