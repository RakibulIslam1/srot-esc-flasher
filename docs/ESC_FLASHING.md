# Flashing Bluejay onto the ESCs (via an Arduino Uno) + why it matters

## Why you need this
SROT runs the thrusters in **bidirectional DShot** so the Pico reads each ESC's **RPM** back. That
RPM is what makes the board:
- **detect the ESC** (an ESC with no telemetry shows "not detected"),
- run the **closed-loop RPM controller** (voltage-independent thrust), and
- give **constant-distance timed moves** (the whole AUTO/move design).

Stock **BLHeli_S** firmware does **not** support bidirectional DShot — so out of the box the board
says "no ESC" even though the motor beeps and spins. **Bluejay** is a drop-in successor to BLHeli_S
that **adds bidirectional DShot + RPM telemetry** to the same ESC hardware. Flash Bluejay once and the
ESC works fully on this board.

> **Safety on this board:** until an ESC reports telemetry, the Pico automatically drives that motor
> **open-loop** (feedforward only, no integrator) so it can never run away. So it's safe to arm/test
> with stock ESCs — you just won't get RPM/closed-loop until Bluejay is flashed.

## What you need
- A spare **ESP32 DevKit V1** as the flashing interface — it bit-bangs a single-pin half-duplex
  link to each ESC (the same technique a real Betaflight/INAV FC uses), no resistor, and can do
  all 4 ESCs at once. *(An Arduino Uno also works with BLHeliSuite's "Make Arduino Interface", but
  the ESP32 env below is the built‑in path and the one this project ships/maintains.)*
- **esc-configurator.com** — the Chrome/Edge web app that flashes Bluejay.
- The ESCs on their **own power** (LiPo/BEC through their normal power leads). **Never** power the
  ESC's main leads from the ESP32/USB.

## Step 1 — Flash the ESP32 as a 4-way interface (from this repo)
A ready-to-flash ESP32 firmware ships as the **`esp32_4way`** env (`src/esp32_4way/`; presents to
esc-configurator like a Betaflight FC over USB, and routes to **4 ESCs**):
```bash
pio run -e esp32_4way -t upload      # from the project root — flashes a spare ESP32 DevKit
```
(See `src/esp32_4way/README.md`.)

**Bringing this up for the first time?** Two hardware-UART/software-serial approaches to the ESC
link were tried and both had ESP32-specific bugs (a shared TX/RX pin silently not transmitting, or
the driver getting torn down). The current firmware instead **bit-bangs the link directly**
(`BitBangSerial.*`), switching the pin's mode between input and output — no resistor, no shared-pin
UART. Before trusting the full esc-configurator flow, flash the diagnostic build and confirm the
physical link works in isolation:
```bash
pio run -e esp32_4way_diag -t upload -t monitor
```
Press `h` for the menu, then wire an ESC to GP16 and press **`i`** — it sends the BLHeli BootInit
handshake and decodes the reply, identifying the ESC's MCU and family:
```
Got 9 bytes: 34 37 31 64 E8 B2 06 01 30
  -> bootloader "471d"  sig=0xE8B2 (EFM8BB21x)
     family: SiLabs BLHeli_S (Bluejay-compatible)
     bootver=6 pages=1  ACK=brSUCCESS
```
Then press **`f`** — the full 4-way self-test. It runs the exact sequence esc-configurator uses
(DeviceInitFlash + the 112-byte settings read at 0x1A00) through `Check_4Way()`, minus USB, and
prints your ESC's **LAYOUT** string and the matching Bluejay hex filename. If `f` passes, the
protocol layer is sound and esc-configurator will work; if it fails, esc-configurator will report
"failed reading ESC".

Also: `b` = bit-timing self-check (no ESC needed), `r` = raw line/edge monitor, `t` = keep-alive.

> **Never put `delay()` between `SendESC()` and `GetESC()`.** The bit-bang receiver is unbuffered
> and polling-only — anything the ESC sends while the code is sleeping is lost forever. Express the
> wait as `GetESC()`'s first-byte timeout instead. (This exact mistake, inherited from the original
> buffered-serial design, made every ESC read fail while the raw link was perfectly fine.)

## Step 2 — Wire the ESCs to the ESP32
| ESP32 | ESC | Notes |
|-------|-----|-------|
| **GP16 / GP17 / GP18 / GP19** | ESC **signal** (1–4) | one wire each, **no resistor** |
| **GND** | ESC **GND** (all) | **common ground is required** |
| — | ESC **power (+/−)** | from a LiPo/BEC — **not** the ESP32 |

Wire 1–4 ESCs. Keep **props off**.

## Which Bluejay firmware / layout?
Releases: **https://github.com/bird-sanctuary/bluejay/releases** — latest stable **v0.21.0**.
(`mathiasvr/bluejay` is the original but dormant since 2022; bird-sanctuary is current.)

Hex files are named **`<LAYOUT>_<MCU>_<DEADTIME>_<PWM>_v<VERSION>.hex`**, e.g. `A_H_5_48_v0.21.0.hex`:

| Field | Meaning |
|---|---|
| **LAYOUT** | `A`…`W` — the ESC's pin mapping **and gate-drive polarity**. Comes from the ESC. |
| **MCU** | `L` = BB1/EFM8BB10 · **`H` = BB2/EFM8BB21** · `X` = BB51 |
| **DEADTIME** | gate blanking (`0`…`120`); `0` only for ESCs whose driver IC makes its own dead time |
| **PWM** | `24` / `48` / `96` kHz |

**Do not choose the layout by hand.** It is stored in the ESC's own settings block (LAYOUT string at
offset 0x40 of the 0x1A00 block, e.g. `#A_H_05#`). esc-configurator reads it and preselects the
matching Bluejay target automatically — that 112-byte read is exactly what the interface has to get
right. The `f` diagnostic above prints the same string, so you can see it yourself and pick the
matching hex manually if you'd rather use BLHeliSuite.

> ⚠️ **Never tick "Ignore MCU layout" / flash a guessed layout.** A layout with the wrong
> `PWM_ACTIVE_HIGH` / `COM_ACTIVE_HIGH` drives the gates backwards, so the high- and low-side FETs
> conduct at the same time — **shoot-through destroys the FETs within milliseconds of the first
> spin-up**. Too small a dead time on an ESC without hardware dead time does the same.

**PWM choice:** **48 kHz** is the general recommendation (smoother low throttle, quieter). Drop to
**24 kHz** if the detected dead time is ≥30 or the thruster desyncs/stutters. BB2 has plenty of
flash for every v0.21.0 `_H_` build. BB21 fully supports **bidirectional DShot / RPM telemetry**
(DShot 300/600 only — no PWM/OneShot), which is what SROT's closed loop needs.

## Step 3 — Flash Bluejay with esc-configurator
1. Open **https://esc-configurator.com** in Chrome/Edge (it uses Web Serial).
2. **Connect** → pick the **ESP32's COM port** (115200) → Connect. It enumerates the ESCs. (If it
   opens then errors, click **Connect** again — the DevKit resets once on port-open.)
3. Select the latest **Bluejay** firmware for your ESC's MCU/layout (it auto-detects the layout).
4. **Flash** (or **Flash All ESCs**). When done, set **Motor Direction = Forward/Reverse (3D mode)**
   on every ESC and **Write Settings**.
5. Repeat for each ESC.

> **There is no "Bidirectional DShot" setting in Bluejay.** Unlike BLHeli_32, Bluejay negotiates
> bidirectional mode from the *inverted* DShot signal the Pico sends — nothing to enable on the ESC.
> **3D mode is mandatory** for SROT: `levelToDshotRaw()` in `src/pico/main.cpp` emits DShot 3D
> framing (0 = stop, 48–1047 reverse, **1048 neutral**, 1049–2047 forward) so the thrusters reverse.

## Step 4 — Back on the SROT board
- **Wiring:** ESC 1–8 signal → Pico **GP6…GP13** (`DSHOT_PIN[]`, `src/pico/main.cpp`), common
  ground. DShot300. Props **off** for the first power-up.
- **Params (Bondor):** `RPM_LOOP = 1` (closed loop) and `DSHOT_BIDIR = 1` (bidir + RPM). Both
  already default to 1 — but if you set `DSHOT_BIDIR = 0` earlier to bench-run stock ESCs, set it
  back. `DSHOT_BIDIR` is applied only while **disarmed** (it rebuilds the PIO drivers).
- **⚠️ Check the motor pole count.** `THR_POLE_PAIRS = 7` is **hardcoded** in `src/pico/main.cpp`
  (assumes a 14-pole motor) and mechanical RPM is `eRPM / THR_POLE_PAIRS`. A different pole count
  makes every RPM reading wrong by a fixed ratio — and since the closed loop *targets* RPM, it will
  hold the wrong speed. Count the magnets inside the bell = poles; pole pairs = poles / 2. Changing
  it needs a Pico reflash, not a param.
- **Scaling:** `RPM_MAX` (default 4000) and `RPM_FF_A` (default 0.00025 ≈ 1/`RPM_MAX`) should match
  the thruster's real top RPM — keep `RPM_FF_A ≈ 1/RPM_MAX` when you change one.
- Power up. In Bondor the thruster should show **detected**, `ESC_STATUS` RPM appears, and the RPM
  closed loop engages (Motor-Tune / constant-distance moves work).
- Note RPM telemetry is **magnitude only**; the sign is taken from the commanded direction
  (`signedRpm()`), so a motor spun backwards by hand still reads positive.

### Verifying the ESCs without motors
You do **not** need motors attached. Bidirectional DShot telemetry comes from the ESC's own MCU in
response to every frame, so a powered ESC with just signal + ground answers and shows **detected**
with **RPM 0**. Wire it up, power the ESCs, stay **disarmed**, and open the **Pico's USB serial
@115200**:
```
link=1 armed=0 bidir=1 rpm_mode=0 loop=1 | M1[rpm=0 pres=1 e=812 d=0 c=0 n=3] ...
```
| Field | Meaning if wrong |
|---|---|
| `bidir=0` | Pico is in plain DShot — **no telemetry exists**. Set `DSHOT_BIDIR = 1` (applies while disarmed). |
| `e=` | eRPM frames decoded. Non-zero ⇒ the whole path works. |
| `d=` | Extended DShot Telemetry frames — ESC alive, just not reporting RPM this frame. |
| `c=` | Replies arriving but failing checksum ⇒ signal integrity / grounding. |
| only `n=` rising | Nothing coming back ⇒ wiring, common ground, or ESC not powered. |

Stay **disarmed** for this test: arming and commanding with no motor attached will (correctly)
latch a no-spin **fault** after 300 ms.

### If a motor only wobbles/judders and never spins
Work this in order — it separates firmware from ESC from wiring instead of guessing:

1. **Read the commanded value.** Hold a motor test and watch the Pico USB serial. Each motor prints
   `cmd=` (what the ESP32 asked for) and `out=` (the DShot value actually clocked out).
   - `out` steady at a healthy forward value (**~1400 for a 15 % test**) with `armed=1` → the
     firmware is doing its job; the fault is in the ESC settings or the motor wiring → step 2.
   - `out` dropping to **0**, or `armed` flickering → the command is being chopped; the disarm
     reason now appears in the GCS log and names the cause.
   - **A steady correct `out=` with a low, *hunting* `rpm=` is a stalling motor, not a firmware
     fault.** E.g. `out=1401` (35 % forward) returning `rpm≈170-190` that jitters: the ESC is
     receiving commands and commutating, it just cannot accelerate the motor. A healthy BLDC at
     35 % is an order of magnitude faster. Go straight to the ESC settings below.
2. **Rule out bidirectional DShot.** Set `DSHOT_BIDIR = 0` (disarmed) → plain DShot, no telemetry →
   motor-test again. If it now spins normally, the ESC misbehaves specifically under bidirectional
   DShot (possibly the 3D-mode + bidir combination) and that is the thing to chase.
3. **ESC settings** (esc-configurator), most likely first. **For Blue Robotics T200s use the full
   profile in [`T200_PROFILE.md`](T200_PROFILE.md)** — the Bluejay defaults are 5-inch-quad values
   and actively fight a large thruster. Summary:
   - **Startup Power** — 1010/1020 is the bottom of the range. A high-inertia thruster with a prop
     needs far more than a quadcopter. Raise it substantially.
   - **PWM frequency vs dead time** — a `?-H-30` layout is dead time **30**; Bluejay's guidance is
     *"the higher your dead time is, the lower your PWM frequency should be."* Reflash at **24 kHz**
     if you are on 48 kHz.
   - **Demag Compensation** Low → **High** for a heavily loaded motor.
4. **Wiring:** all four motors wobbling identically → settings. Only one or two → suspect a
   **bad or missing motor phase** on those (the classic mechanical cause of wobble-without-spin);
   swap that motor onto a known-good ESC to confirm.

> Note: "Motor test ended - disarmed" **after you release the button** is normal — the GCS stops
> refreshing the keep-alive and the vehicle disarms by design (ArduSub `verify_motor_test()`).

> **Maintainer gotcha:** telemetry must be read **before** sending the next frame, not after the
> current one. The ESC only starts replying ~30 µs after a frame ends, so a send-then-immediately-
> read ordering returns `NO_PACKET` every time and the ESC is never detected. The Pico output loop
> runs at a fixed `OUT_PERIOD_US` cadence precisely so the period doubles as the reply window —
> that cadence also defines what `RPM_KI` ("integral step per cycle") means, so don't remove it.

## Notes / gotchas
- **Bluejay only runs on SiLabs EFM8 (BLHeli_S) ESCs.** The `i` diagnostic above tells you exactly
  which family you have. Signatures `0xE8B1/0xE8B2/0xE8B5` = EFM8BB10x/BB21x/BB51x = BLHeli_S =
  Bluejay-compatible. An ARM signature (BLHeli_32 / AM32) is **not** Bluejay-flashable, and a
  PWM-only ESC has no bootloader at all and will never answer.
- The interface reports the ESC family to esc-configurator, derived from that signature. This
  matters: SiLabs parts use **512-byte** flash pages and ARM parts **1024-byte** — reporting the
  wrong family erases the wrong flash regions.
- If esc-configurator can't see the ESC: check the **common ground**, that the ESC has **external
  power**, and that you wired the ESC signal to the pin you think you did (ESC 1 = **GP16**).

## Sources
- [Flashing BLHeli firmware using an Arduino (Flite Test)](https://www.flitetest.com/articles/how-to-flashing-blheli-firmware-using-arduino)
- [Flash ESCs with ANY Arduino (Flite Test)](https://www.flitetest.com/articles/flash-escs-with-any-arduino)
- [Bluejay firmware](https://github.com/aocodarc/bluejay)
- [ESC firmware / esc-configurator (Clover docs)](https://clover.coex.tech/en/esc_firmware.html)
