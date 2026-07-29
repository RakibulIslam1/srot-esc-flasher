# ESP32 DevKit → BLHeli/Bluejay 4‑way interface (flash up to 4 ESCs at once)

Turns a spare **ESP32 DevKit V1** into a **4‑way interface** so **esc‑configurator.com** can read,
configure, and **flash Bluejay** onto your BLHeli_S ESCs — exactly like a Betaflight FC's passthrough,
and **up to 4 ESCs at once**. Bluejay adds **bidirectional DShot**, which the SROT board needs for RPM
(ESC "detected", closed‑loop RPM, constant‑distance moves).

This is the **`esp32_4way`** env of the project (compiles only `src/esp32_4way/`). Source is vendored
from [BrushlessPower/BlHeli‑Passthrough](https://github.com/BrushlessPower/BlHeli-Passthrough) (ESP32/,
see `LICENSE`); the **4‑ESC routing** (index → pin) is a SROT addition. The ESC link (`BitBangSerial.*`)
is a hand‑written single‑pin **bit‑bang** half‑duplex UART that switches the pin's *mode* between
input and output around each direction change — the same technique Betaflight/INAV use on a real FC
(`serial_4way_avrootloader.c`) — **no resistor needed**, and no `EspSoftwareSerial`/hardware‑UART
pin‑sharing (both were tried and both have documented ESP32‑specific bugs — see `docs/ESC_FLASHING.md`).

## 1. Flash the ESP32
```bash
pio run -e esp32_4way -t upload      # from the project root
```
(or pick the **esp32_4way** env in the VS Code PlatformIO status bar → Upload.)

**First time / re‑bringing‑up the hardware?** Flash `esp32_4way_diag` instead and open the serial
monitor — a plain‑text bench tool to confirm the physical link works *before* trusting the full
esc‑configurator flow:
```bash
pio run -e esp32_4way_diag -t upload -t monitor
```
Press `h` for the menu. The important one is **`i`** — wire an ESC to GP16, press `i`, and it sends
the BLHeli BootInit handshake and decodes the reply, e.g.:
```
Got 9 bytes: 34 37 31 64 E8 B2 06 01 30
  -> bootloader "471d"  sig=0xE8B2 (EFM8BB21x)
     family: SiLabs BLHeli_S (Bluejay-compatible)
     bootver=6 pages=1  ACK=brSUCCESS
```
That's a healthy link. Then press **`f`** — the full 4‑way self‑test, which runs the exact sequence
esc‑configurator uses (DeviceInitFlash + the 112‑byte settings read at 0x1A00) through
`Check_4Way()` and prints your ESC's **LAYOUT** string plus the matching Bluejay hex filename. `f`
passing means esc‑configurator will work. Also: `b` = bit‑timing self‑check (no ESC needed),
`r` = raw line/edge monitor, `t` = keep‑alive ping.

> **Maintainer warning:** never put a `delay()` between `SendESC()` and `GetESC()`. The bit‑bang
> receiver is unbuffered and polling‑only, so any byte the ESC sends while the code sleeps is gone.
> Express the wait as `GetESC()`'s first‑byte timeout. Likewise, don't add `yield()` to
> `bbReadByte()`'s start‑edge poll — a task switch there shifts every sample by one bit.

## 2. Wire the ESCs
| ESP32 DevKit | ESC | Notes |
|---|---|---|
| **GP16** | ESC 1 **signal** | one wire, no resistor |
| **GP17** | ESC 2 **signal** | |
| **GP18** | ESC 3 **signal** | |
| **GP19** | ESC 4 **signal** | (wire only the ones you're flashing) |
| **GND** | ESC **GND** (all) | **common ground required** |
| — | ESC **power (+/−)** | from a LiPo/BEC — **never** from the ESP32/USB |

**Props off.** You can wire 1–4 ESCs; esc‑configurator shows 4 slots and detects whichever answer.

## 3. Flash Bluejay with esc‑configurator
1. Open **https://esc-configurator.com** (Chrome/Edge).
2. **Connect** → pick the ESP32's COM port (115200). It enumerates the ESCs.
   - If it opens then errors, click **Connect** again (the DevKit resets once on port‑open; the 2nd try lands).
3. For each ESC: select **Bluejay v0.21.0 @ 24 kHz** → **Flash**.
4. Set **Motor Direction = Forward/Reverse (3D mode)** on every ESC — SROT drives DShot 3D framing
   (0 = stop, 48–1047 reverse, 1048 neutral, 1049–2047 forward). Then **Write Settings**.
   *(Bluejay has **no** "Bidirectional DShot" setting — unlike BLHeli_32 it negotiates bidir mode
   from the inverted signal the Pico sends, so there is nothing to enable on the ESC.)*

## 4. Back on the SROT board
Reconnect the ESCs to the thruster harness (Pico DShot outputs). They now show **detected**, RPM
appears, and (with `RPM_LOOP = 1`, `DSHOT_BIDIR = 1`) the **closed‑loop RPM** works. Full guide:
[`../../docs/ESC_FLASHING.md`](../../docs/ESC_FLASHING.md).

## Notes
- Pins are set in `Global.cpp` (`ESC_PIN[] = {16,17,18,19}`) — change them there if you prefer other
  GPIOs (keep them output‑safe: avoid 0/2/12/15 strapping and 34–39 input‑only).
- Works with **DShot BLHeli_S / Bluejay** (SiLabs EFM8) ESCs, not BLHeli_32 (ARM).
- If an ESC won't detect: check the **common ground**, that it has **external power**, and the right pin.
