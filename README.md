# SROT — ESC Flasher / 4-Way Debug Interface

Turns a spare **ESP32 DevKit V1** into a **BLHeli 4-way interface** so
[esc-configurator.com](https://esc-configurator.com) can read, configure and **flash Bluejay** onto
BLHeli_S ESCs — exactly like a Betaflight flight controller's passthrough, and **up to 4 ESCs at
once**.

Part of the **SROT** AUV control-board suite:

| Repo | What it is |
|---|---|
| [srot-control-board](https://github.com/RakibulIslam1/srot-control-board) | ESP32 flight controller + RP2350 thruster co-processor |
| [srot-ground-station](https://github.com/RakibulIslam1/srot-ground-station) | Bondor desktop GCS + ESP32-C3 LoRa bridge |
| **srot-esc-flasher** *(this repo)* | ESC flashing / 4-way debug |

---

## Why you need it

SROT drives the thrusters with **bidirectional DShot** to read each ESC's **RPM** back — that RPM is
what gives thruster health detection and repeatable, battery-independent thrust. Stock **BLHeli_S**
does not support bidirectional DShot. **Bluejay** is a drop-in successor that adds it to the same
hardware. This board is how you flash it.

## Build & flash

```bash
pio run -e esp32_4way -t upload          # the flasher
pio run -e esp32_4way_diag -t upload -t monitor   # bench diagnostics (see below)
```

One `platformio.ini`, two envs. No external libraries, no shared headers — this project is
self-contained.

## Wiring

| ESP32 DevKit | ESC | Notes |
|---|---|---|
| **GP16 / GP17 / GP18 / GP19** | ESC 1–4 **signal** | one wire each, **no resistor** |
| **GND** | ESC **GND** | **common ground is required** |
| — | ESC **power** | from a LiPo/BEC — **never** from the ESP32 |

**Props off.** Wire 1–4 ESCs; esc-configurator shows 4 slots and detects whichever answer.

## Diagnostic build

If esc-configurator won't connect, flash `esp32_4way_diag` and open the serial monitor at 115200.
Press `h` for the menu. The important one is **`i`** — it sends the BLHeli BootInit handshake and
decodes the reply:

```
Got 9 bytes: 34 37 31 64 E8 B2 06 01 30
  -> bootloader "471d"  sig=0xE8B2 (EFM8BB21x)
     family: SiLabs BLHeli_S (Bluejay-compatible)
     bootver=6 pages=1  ACK=brSUCCESS
```

That confirms the physical link before you trust the full protocol stack. Also: `b` bit-timing
self-check, `r` raw line monitor, `t` keep-alive, `f` full 4-way self-test (which prints your ESC's
**LAYOUT** string and the matching Bluejay hex filename).

## Docs

- [`docs/ESC_FLASHING.md`](docs/ESC_FLASHING.md) — full workflow, plus a troubleshooting ladder for
  "the motor only wobbles and won't spin"
- [`src/esp32_4way/README.md`](src/esp32_4way/README.md) — pin config and usage notes

## Implementation note

The ESC link is a **hand-written single-pin bit-bang half-duplex UART**
([`BitBangSerial.cpp`](src/esp32_4way/BitBangSerial.cpp)) that switches the GPIO's *mode* between
input-pullup and push-pull output around each direction change — the same technique Betaflight/INAV
use (`serial_4way_avrootloader.c`). Two earlier approaches were tried and abandoned:
`EspSoftwareSerial` on a shared pin, and the ESP-IDF hardware UART with TX and RX on one GPIO. Both
have documented ESP32-specific bugs (see esp-idf#14787 and arduino-esp32#12729) and neither worked
reliably.

Two maintainer rules, both learned the hard way:
- **Never put a `delay()` between `SendESC()` and `GetESC()`** — the receiver is unbuffered and
  polling-only, so anything the ESC sends while the code sleeps is lost.
- **Never add `yield()` to `bbReadByte()`'s start-edge poll** — a task switch there shifts every
  sample by one bit and silently corrupts the first byte.

## Licence

`src/esp32_4way/` is vendored from
[BrushlessPower/BlHeli-Passthrough](https://github.com/BrushlessPower/BlHeli-Passthrough) (ESP32
folder) — see [`src/esp32_4way/LICENSE`](src/esp32_4way/LICENSE). The 4-ESC routing, the bit-bang
transport, the SiLabs/ARM interface-mode detection and the diagnostic build are SROT additions.
