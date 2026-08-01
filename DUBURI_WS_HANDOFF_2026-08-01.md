# Handoff to the srot-esc-flasher agent — 2026-08-01

**From:** the `duburi_ws` (Mongla) companion side.
**Against:** `srot-esc-flasher` @ `93337d1` · board `e2b43fe` · **`SROT_FW_BEHAVIOUR_REV 2`**.

**No code change is requested and none is expected.** This repo is a one-time bench tool and
its job is done well. One thing changed downstream that makes your output *observable* for the
first time — worth knowing, because until now a Bluejay flash could not be verified end to end.

---

## Per-thruster RPM now reaches ROS — the flash is finally checkable

Bidirectional DShot RPM depended on Bluejay, which is what this tool exists to install. But the
companion could never *see* it, for a reason that had nothing to do with the ESCs:

- The board emitted `ESC_STATUS` (**291**), and upstream MAVLink removed 290/291 from the
  `common` dialect. **291 is in no pymavlink dialect we ship.**
- pymavlink **silently discards** any msgid missing from its CRC-extra table — no error, no
  callback. So the board could stream perfect RPM while the host read nothing.
- Separately, pymavlink binds **one** dialect at import and the default is
  `dialects.v10.ardupilotmega` — **MAVLink 1**, where `ESC_TELEMETRY_1_TO_4` (11030) does not
  exist either.

Both are fixed. The board now also emits **`ESC_TELEMETRY_1_TO_4` / `_5_TO_8` (11030/11031)**,
and the companion forces MAVLink 2 before importing pymavlink. **`/duburi/esc_rpm` publishes.**

**Why this matters to you:** zero RPM on a thruster is no longer ambiguous. Previously it could
mean a dead ESC, a wiring fault, a failed Bluejay flash, *or* a decode that was never going to
work. Now it means one of the first three — so a flash can actually be verified:

```bash
ros2 topic echo /duburi/esc_rpm     # 8 values; all-zero armed = a real fault, not a decode bug
```

Two caveats worth carrying in your docs, since they will otherwise read as flasher bugs:

- **RPM in `ESC_TELEMETRY` is `uint16`, so the board sends magnitude.** Direction lives in the
  commanded value. Do not read these as signed velocity.
- **Temperature / voltage / current are `0` = *not instrumented***, not "measured zero". The
  Pico link does not report them yet. An ESC reading 0 °C is not a sensor fault.

---

## The precondition still stands, and is now enforced upstream

**Bluejay before flight**, exactly as your `README.md` says: stock BLHeli_S has no bidirectional
DShot, so RPM is absent, and with it `THR_TRIM_EN` (per-thruster RPM trim) and thruster-health
detection. Nothing about that changed — it is just that a missed flash is now *visible* instead
of blending into a decode failure.

Note `THR_POLE_PAIRS` is **compile-time** on the board (7, correct for T200). A different motor
needs a firmware rebuild, not a parameter — worth flagging if anyone swaps thrusters.

---

## Standing invariants (unchanged)

Nothing in this repo touches the co-owned wire contract, and it should stay that way. This tool
talks BLHeli 4-way to the ESCs; it is not on the MAVLink path, not in the control loop, and not
something a mission depends on at runtime. **The correct amount of coupling to `duburi_ws` is
zero** — see `AGENTS.md`.
