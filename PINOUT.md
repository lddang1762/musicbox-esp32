# ESP32 Music Box — Pinout & Circuit Connections

All pin numbers are ESP32 **GPIO numbers** (not board header positions) and match [config.h](config.h) and [esp32-test.ino](esp32-test.ino). If you change a pin there, update this file.

## Summary

| GPIO | Function | Connected to | Notes |
|------|----------|--------------|-------|
| 21 | I2C SDA | OLED SDA, PCA9685 SDA | Shared bus |
| 22 | I2C SCL | OLED SCL, PCA9685 SCL | Shared bus |
| 18 | SPI SCK | SD module SCK / CLK | |
| 19 | SPI MISO | SD module MISO | |
| 23 | SPI MOSI | SD module MOSI | |
| 5 | SD chip select | SD module CS / SS | Driven HIGH at boot |
| 27 | Button: Previous | Button → GND | `INPUT_PULLUP` |
| 26 | Button: Play / Pause | Button → GND | `INPUT_PULLUP` |
| 25 | Button: Next | Button → GND | `INPUT_PULLUP` |
| 33 | Button: Stop | Button → GND | `INPUT_PULLUP` |
| 32 | Button: Power | Button → GND | `INPUT_PULLUP` |
| — | `POWER_PIN` | Not used (`-1`) | Set in config.h to switch an external power stage |

GPIOs not listed are unused.

## I2C bus (SDA 21 / SCL 22, 400 kHz)

Two devices share one bus, so wire them in parallel.

| Device | Address | Supply |
|--------|---------|--------|
| 2.42" SSD1309 OLED, 128x64 | `0x3C` | 3.3 V |
| PCA9685 servo driver | `0x40` | VCC (logic) 3.3 V; V+ (servo power) 5 V |

```
ESP32 GPIO21 ──┬── OLED SDA
               └── PCA9685 SDA
ESP32 GPIO22 ──┬── OLED SCL
               └── PCA9685 SCL
ESP32 3V3    ──┬── OLED VCC
               └── PCA9685 VCC
ESP32 GND    ──┬── OLED GND
               └── PCA9685 GND
```

- **OLED reset:** `OLED_RESET` is `-1`, so the module's RES pin must be tied high on its board. The SSD1309 has no reliable power-on reset. If your module brings RES out to a header and the screen stays blank, wire RES to a free GPIO and set `OLED_RESET` in config.h.
- **Pull-ups:** most OLED and PCA9685 breakouts already have I2C pull-ups. If the bus is flaky, add 4.7 kΩ from SDA and SCL to 3.3 V, but only one set.

## Servo (continuous-rotation SG90 on PCA9685 channel 0)

| SG90 wire | Connect to |
|-----------|------------|
| Brown / black (GND) | PCA9685 channel 0, GND pin |
| Red (+) | PCA9685 channel 0, V+ pin |
| Orange / yellow (signal) | PCA9685 channel 0, PWM pin |

- Power the PCA9685 **V+ rail from a separate 5 V supply**, not the ESP32's 3.3 V pin. A servo stall can pull several hundred mA.
- **Join the grounds.** The 5 V supply's GND, the PCA9685 GND and the ESP32 GND must all be common.
- Add a bulk capacitor (470–1000 µF) across the PCA9685 V+ and GND to absorb servo current spikes.
- The servo spins while a song plays. Speed and direction are tuned with `SERVO_OFFSET` in config.h or at runtime with `GET /api/servo?speed=N`.

## SD card module (SPI)

| SD module | ESP32 |
|-----------|-------|
| VCC | See note below |
| GND | GND |
| SCK / CLK | GPIO 18 |
| MISO | GPIO 19 |
| MOSI | GPIO 23 |
| CS / SS | GPIO 5 |

- **VCC:** use the supply your module expects. A bare TF module is 3.3 V only. A module with an onboard regulator and level shifter takes 5 V. The firmware waits for the rail to settle before touching the card, because WiFi current bursts can brown it out.
- The card must be FAT-formatted with songs (`.mid` / `.midi`) in a `/MUSIC` folder.
- Card init needs a solid 3.3 V. If it fails intermittently, add a 10–100 µF capacitor near the module's VCC.

## Buttons

Each button is wired **between its GPIO and GND**. No external resistor is needed: the firmware enables the internal pull-up, so idle reads HIGH and a press reads LOW (50 ms debounce).

```
GPIO27 ──[Prev]───── GND
GPIO26 ──[Play]───── GND
GPIO25 ──[Next]───── GND
GPIO33 ──[Stop]───── GND
GPIO32 ──[Power]──── GND
```

| Button | Behavior |
|--------|----------|
| Prev | Song select: cursor up. Now playing: previous song |
| Play / Pause | Song select: play highlighted song. Now playing: toggle |
| Next | Song select: cursor down. Now playing: next song |
| Stop | Return to song select |
| Power | Toggle the box on / off (2 s transition) |

## Power

| Rail | Feeds |
|------|-------|
| ESP32 USB / VIN | The ESP32 itself |
| 3.3 V (ESP32 3V3) | OLED, PCA9685 logic, SD card (if 3.3 V module) |
| 5 V (separate supply) | PCA9685 V+ → servo, AD828 `VCC+`, PAM8403 `5V+` |

All grounds must be common.

## Network

No wiring. The box joins the WiFi network set in config.h (`WIFI_SSID`, `WIFI_PASSWORD`) and is reachable at `http://musicbox.local`.

## Audio chain (analog, not connected to the ESP32)

Piezo pickup → AD828 → pot → PAM8403 → speaker. The ESP32 doesn't touch any of it in the current firmware. Volume is the manual potentiometer. Only the left channel is wired.

| From | To |
|------|----|
| Piezo disc `+` / `-` | AD828 `IN+` / `IN-` |
| 5 V rail | AD828 `VCC+` |
| AD828 `VCC-` | Common ground |
| AD828 `OUT L` | 4.7 kΩ resistor → potentiometer input |
| Potentiometer output (wiper) | PAM8403 `IN L` |
| Potentiometer ground | Common ground |
| 5 V rail | PAM8403 `5V+` |
| PAM8403 `5V-` | Common ground |
| PAM8403 `OUT L +` / `-` | Speaker `+` / `-` (10 Ω) |

```
Piezo ──► AD828 ──► OUT L ──[4.7k]──► POT (in) ──► wiper ──► PAM8403 IN L ──► OUT L +/- ──► Speaker
                                       │
                                       └─ pot ground ──► common GND

5 V rail ──► AD828 VCC+      5 V rail ──► PAM8403 5V+
GND      ──► AD828 VCC-      GND      ──► PAM8403 5V-
```

- Both ICs run from the same 5 V rail and share one ground, so the AD828 is a single-supply stage.
- The 5 V rail can be the same supply that feeds the PCA9685 V+ rail. All grounds must be common with the ESP32.

## Bring-up checklist

1. Serial monitor at 115200 baud. The boot log should show `[OLED] Initialized`, `[SERVO] Initialized (stopped)` and `[SD] Card initialized`.
2. `PCA9685 not found — servo disabled` means check SDA/SCL, VCC and the address (`0x40`).
3. `[OLED] Initialization failed` means check the address (`0x3C`, some modules use `0x3D`) and the RES pin.
4. SD failures: check CS (GPIO 5), the `/MUSIC` folder, FAT formatting and the supply voltage.
5. A button doing nothing: check it goes to GND, not 3.3 V.
