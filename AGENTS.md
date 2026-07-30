# AGENTS.md — srot-esc-flasher

Instructions for AI agents and developers working in this repo. A matching file exists in each
sibling repo; the **Shared invariants** section below is deliberately identical in all of them.

---

## What this repo is

A spare **ESP32 DevKit V1** turned into a **BLHeli 4-way interface**. Over USB it impersonates a
Betaflight FC's MSP passthrough so **esc-configurator.com** (Web Serial) can read, configure and
flash up to **4 ESCs at once**.

It is a **one-time bench tool**, run per ESC before that ESC goes on the vehicle. It is not on
the vehicle, not on the MAVLink network, and has no runtime relationship with anything else.
`platformio.ini` says it plainly: self-contained, no shared headers, no vendored MAVLink, no
external libraries.

## Where it sits

```
[bench]  ESP32 4-way flasher ── 4× ESC signal wires ── ESCs (off the vehicle)
                    │
              esc-configurator.com (Chrome/Edge Web Serial)
```

## Why it matters to the rest of the system

Stock **BLHeli_S does not support bidirectional DShot**, so an un-flashed ESC reports no
telemetry and the board reads "no ESC" even though the motor beeps and spins. Flashing
**Bluejay** is the precondition for:

1. ESC presence detection,
2. the Pico's closed-loop RPM controller,
3. constant-distance timed moves — i.e. the whole `SROT_MOVE` design.

Until an ESC reports telemetry the Pico drives that motor **open-loop** (feedforward only, no
integrator) so it can never run away — it is safe to arm and test with stock ESCs.

---

## Shared invariants (identical in every repo's AGENTS.md)

These are **co-owned across repos**. Changing one unilaterally breaks a partner silently.
**None of them are reachable from this repo** — it has no MAVLink and no shared headers — but
they are listed so the set is the same everywhere and so a change here is never mistaken for a
system-wide one.

1. **The wire constants are frozen unless changed on both sides in the same PR.**
   `MAV_CMD_SROT_MOVE = 31000`; the `SROT_MOVE` p1 type codes and their ordering; the
   `FlightMode` integers; `PCA_RELAY_BASE_CH = 8`; `MAVLINK_BAUD = 115200`;
   `GCS_FAILSAFE_MS = 5000`.
2. **`movement::Type` is append-only.**
3. **Every command reaches exactly one terminal ACK.**
4. **Depth sign: `VFR_HUD.alt` is negative below the surface.**
5. **A heartbeat ≥ 1 Hz is mandatory** or the board surfaces.
6. **Never break the contract to fix a bug** — coordinate instead.

---

## Rules specific to this repo

**The two timing rules, both learned the hard way and both in the README:**

- **Never `delay()` between `SendESC()` and `GetESC()`.** The bit-bang receiver is unbuffered
  and polling-only; bytes that arrive while it is asleep are gone.
- **Never add `yield()` to `bbReadByte()`'s start-edge poll.** A task switch shifts every
  sample by one bit.

**Never tick "Ignore MCU layout" in esc-configurator.** A wrong `PWM_ACTIVE_HIGH` /
`COM_ACTIVE_HIGH` drives the gates backwards and shoot-through destroys the FETs within
milliseconds of the first spin-up.

**Motor direction must be set to Forward/Reverse (3D mode)** — the board's Pico emits DShot 3D
framing (`0` stop, `48–1047` reverse, `1048` neutral, `1049–2047` forward). This is not optional.

**Interface-mode defaults matter.** `CurrentInterfaceMode` initialises to `imARM_BLB` and is
only corrected at `DeviceInitFlash`. ARM uses 1024-byte flash pages, SiLabs 512 — an operation
before init would use the wrong stride.

**Buffer sizing.** A full 256-byte flash read replies with **259** bytes (256 + ACK + 2 CRC);
the default `GetESC()` cap of 250 silently truncated it and corrupted every full-page read.

**Use the diagnostic build** (`esp32_4way_diag`) before suspecting hardware. `b` checks bit
timing with no ESC attached; `f` runs a full 4-way self-test and prints the layout/MCU/name,
from which the correct Bluejay hex filename is derived. **`f` passing means esc-configurator
will work.**

**Known documentation defect:** `docs/ESC_FLASHING.md` references `docs/T200_PROFILE.md`, which
**does not exist** in this repo. The authoritative T200 profile lives in
`srot-control-board/docs/`. Either fix the link or write the file — do not leave it dangling.

## Downstream settings this tool's work depends on

Set on the **board** (via Bondor), not here: `RPM_LOOP`, `DSHOT_BIDIR` (applied disarmed only —
it rebuilds the PIO drivers), `RPM_MAX`, `RPM_FF_A` (keep ≈ `1/RPM_MAX`).

⚠️ **`THR_POLE_PAIRS = 7` is hardcoded** in the Pico firmware (assumes a 14-pole motor).
Mechanical RPM = `eRPM / THR_POLE_PAIRS`. A wrong pole count makes every RPM reading off by a
fixed ratio — and because the loop *targets* RPM, it will confidently hold the wrong speed.
Changing it needs a Pico reflash, not a parameter.
