// =============================================================================
//  ESP32 (DevKit V1) → BLHeli/Bluejay 4-way interface for esc-configurator.com
//
//  Presents to esc-configurator over USB serial exactly like a Betaflight FC's MSP
//  passthrough, so you can read / flash Bluejay / configure up to 4 ESCs at once —
//  no resistor needed (single-pin bit-bang half-duplex per ESC, see BitBangSerial.*).
//  BLE stripped (USB only).
//
//  Source vendored from github.com/BrushlessPower/BlHeli-Passthrough (ESP32/), see
//  LICENSE. 4-ESC routing + pins are SROT additions (Global.* / ESC_Serial.* / 4Way.cpp).
//  ESC signals → GP16/17/18/19, common GND, ESCs on their own power.
//
//  Bench bring-up: build the `esp32_4way_diag` env instead (adds -D ESC4WAY_DIAG_MODE)
//  for a plain-text menu over the USB serial monitor that checks the bit-bang timing and
//  decodes a real ESC's bootloader identity, before trusting the full esc-configurator
//  flow. See src/esp32_4way/README.md.
// =============================================================================

#include <Arduino.h>
#include "Global.h"
#include "serial_comm.h"

#ifndef ESC4WAY_DIAG_MODE

void setup() {
  Serial.begin(115200);
}

void loop() {
  process_serial();   // MSP + 4-Way over USB serial
}

#else  // ESC4WAY_DIAG_MODE — bench bring-up tool, plain-text menu over the USB serial monitor

#include "ESC_Serial.h"
#include "BitBangSerial.h"
#include "4Way.h"        // CMD_KEEP_ALIVE, brSUCCESS, etc.

static void printHelp() {
  Serial.println();
  Serial.println("== esp32_4way diagnostic build ==");
  Serial.println("b = bit-timing self-check (no ESC, no jumper needed)");
  Serial.println("r = raw line/edge monitor on GP16 for 3s (wire an ESC, watch idle level)");
  Serial.println("t = send InterfaceTestAlive keep-alive to GP16, dump raw reply");
  Serial.println("i = send BLHeli BootInit handshake to GP16, decode the ESC's identity");
  Serial.println("f = FULL 4-way self-test: what esc-configurator does, minus USB");
  Serial.println("    (InitFlash + read the 0x1A00 settings block -> LAYOUT / firmware hex)");
  Serial.println("h = this help");
}

// Verifies the bit-bang timing base by measuring how long a byte actually takes to
// clock out: 10 bit-times (start + 8 data + stop) at 52us should be ~520us. A few
// percent of error is fine; a large error means the busy-wait timing is off.
//
// (There is deliberately NO TX->RX loopback test here: bbWriteByte() blocks until the
// whole byte is sent, so a single-threaded write-then-read can never catch its own
// start bit and would always report a false failure.)
static void cmdBitTiming() {
  const uint16_t N = 50;
  bbInit(ESC_PIN[0]);
  bbSetOutput(ESC_PIN[0]);
  uint32_t t0 = micros();
  for (uint16_t i = 0; i < N; i++) bbWriteByte(ESC_PIN[0], 0x55);
  uint32_t el = micros() - t0;
  bbSetInput(ESC_PIN[0]);

  float per_byte = (float)el / (float)N;
  float per_bit  = per_byte / 10.0f;
  float err_pct  = (per_bit - (float)BB_BIT_TIME_US) * 100.0f / (float)BB_BIT_TIME_US;
  Serial.printf("Bit timing: %.1f us/byte, %.2f us/bit (target %u), error %+.1f%% -> %s\n",
                per_byte, per_bit, BB_BIT_TIME_US, err_pct,
                (err_pct > -3.0f && err_pct < 3.0f) ? "OK" : "OUT OF SPEC");
}

static void cmdRawMonitor() {
  Serial.println("Raw monitor on GP16 for 3s (prints every level change)...");
  pinMode(ESC_PIN[0], INPUT_PULLUP);
  uint32_t t0 = millis();
  int last = digitalRead(ESC_PIN[0]);
  Serial.printf("t=0ms level=%d (idle — should be 1/high if pulled up correctly)\n", last);
  while (millis() - t0 < 3000) {
    int lvl = digitalRead(ESC_PIN[0]);
    if (lvl != last) {
      Serial.printf("t=%lums level=%d\n", (unsigned long)(millis() - t0), lvl);
      last = lvl;
    }
  }
  Serial.println("done");
}

static void sendAndDump(uint8_t *frame, uint16_t len, bool crc, uint16_t wait_ms) {
  selectEsc(0);   // GP16
  SendESC(frame, len, crc);
  uint8_t rx[300];
  uint16_t n = GetESC(rx, wait_ms);
  Serial.printf("Sent %u bytes, got %u bytes back:", len, n);
  for (uint16_t i = 0; i < n; i++) Serial.printf(" %02X", rx[i]);
  Serial.println(n == 0 ? "  (nothing)" : "");
}

static void cmdKeepAlive() {
  uint8_t f[2] = {CMD_KEEP_ALIVE, 0};
  sendAndDump(f, 2, true, 200);
}

// Decode the BLHeli bootloader's identity reply:
//   "471x" + SIGNATURE_001 + SIGNATURE_002 + BootVersion + BootPages + ACK(brSUCCESS)
// The 4th char is the bootloader REVISION, not a platform flag — a SiLabs BLHeli_S part
// legitimately answers "471d".
static void cmdBootInit() {
  uint8_t f[] = {0, 0, 0, 0, 0, 0, 0, 0, 0x0D, 'B', 'L', 'H', 'e', 'l', 'i', 0xF4, 0x7D};
  selectEsc(0);                            // GP16
  SendESC(f, sizeof(f), false);            // BootInit's CRC is already inlined in the frame
  uint8_t rx[300];
  uint16_t n = GetESC(rx, 200, sizeof(rx));

  Serial.printf("Got %u bytes:", n);
  for (uint16_t i = 0; i < n; i++) Serial.printf(" %02X", rx[i]);
  Serial.println();

  if (n < 9 || rx[0] != '4' || rx[1] != '7' || rx[2] != '1') {
    Serial.println("  -> no valid \"471x\" bootloader reply. Press 'i' again: a one-off bad");
    Serial.println("     first byte is a mis-synced start bit, not a hardware fault. If it");
    Serial.println("     repeats, check GP16 wiring / common ground / power-cycle the ESC.");
    return;
  }
  uint16_t sig = ((uint16_t)rx[4] << 8) | rx[5];
  const char *mcu = "unknown";
  const char *fam = "unknown";
  if      (sig == 0xE8B1) { mcu = "EFM8BB10x"; fam = "SiLabs BLHeli_S (Bluejay-compatible)"; }
  else if (sig == 0xE8B2) { mcu = "EFM8BB21x"; fam = "SiLabs BLHeli_S (Bluejay-compatible)"; }
  else if (sig == 0xE8B5) { mcu = "EFM8BB51x"; fam = "SiLabs BLHeli_S (Bluejay-compatible)"; }
  else if (sig > 0xE800 && sig < 0xF900) {  mcu = "SiLabs";  fam = "SiLabs BLHeli_S"; }
  else if (rx[4] > 0x00 && rx[4] < 0x90 && rx[5] == 0x06) { mcu = "ARM"; fam = "BLHeli_32 / AM32 (NOT Bluejay)"; }

  Serial.printf("  -> bootloader \"471%c\"  sig=0x%04X (%s)\n", rx[3], sig, mcu);
  Serial.printf("     family: %s\n", fam);
  Serial.printf("     bootver=%u pages=%u  ACK=%s\n",
                rx[6], rx[7], (rx[n - 1] == brSUCCESS) ? "brSUCCESS" : "FAIL");
}

// ---------------------------------------------------------------------------
// Full 4-way self-test: drives Check_4Way() with hand-built frames — the exact
// sequence esc-configurator issues, minus USB. This exercises the protocol layer
// (where the reply-swallowing delay() bugs lived); the 'i' test above only covers
// the raw SendESC/GetESC transport.
// ---------------------------------------------------------------------------

// Build a 4-way request in `buf` and run it through Check_4Way(); returns the ACK byte.
static uint8_t run4Way(uint8_t *buf, uint8_t cmd, uint8_t addr_hi, uint8_t addr_lo,
                       const uint8_t *params, uint8_t plen, uint16_t *o_len) {
  buf[0] = cmd_Local_Escape;
  buf[1] = cmd;
  buf[2] = addr_hi;
  buf[3] = addr_lo;
  buf[4] = plen;                                   // 0 means 256 per the protocol
  uint8_t n = (plen == 0) ? 256 : plen;
  for (uint8_t i = 0; i < n; i++) buf[5 + i] = params ? params[i] : 0;
  uint16_t crc = 0;
  for (uint16_t i = 0; i < (uint16_t)(5 + n); i++) crc = _crc_xmodem_update(crc, buf[i]);
  buf[5 + n]     = (crc >> 8) & 0xFF;
  buf[5 + n + 1] = crc & 0xFF;

  Check_4Way(buf);                                 // replies in-place
  uint16_t olen = buf[4];
  if (o_len) *o_len = olen;
  return buf[olen + 5];                            // ACK byte
}

static const char *ackName(uint8_t a) {
  switch (a) {
    case ACK_OK:                return "ACK_OK";
    case ACK_I_INVALID_CMD:     return "ACK_I_INVALID_CMD";
    case ACK_I_INVALID_CRC:     return "ACK_I_INVALID_CRC";
    case ACK_I_INVALID_CHANNEL: return "ACK_I_INVALID_CHANNEL";
    case ACK_I_INVALID_PARAM:   return "ACK_I_INVALID_PARAM";
    case ACK_D_GENERAL_ERROR:   return "ACK_D_GENERAL_ERROR";
    default:                    return "?";
  }
}

// Print a fixed-width field from the settings block as text (BLHeli pads with 0xFF).
static void printField(const char *label, const uint8_t *p, uint8_t len) {
  Serial.printf("     %-7s: \"", label);
  for (uint8_t i = 0; i < len; i++) {
    uint8_t c = p[i];
    if (c == 0xFF || c == 0x00) break;
    Serial.print((c >= 32 && c < 127) ? (char)c : '.');
  }
  Serial.println("\"");
}

static void cmdFullSelfTest() {
  static uint8_t buf[320];
  uint16_t olen = 0;

  Serial.println("\n-- 1. cmd_DeviceInitFlash(target=0) --");
  uint8_t p0 = 0;
  uint8_t ack = run4Way(buf, cmd_DeviceInitFlash, 0, 0, &p0, 1, &olen);
  Serial.printf("   ACK = 0x%02X (%s)\n", ack, ackName(ack));
  if (ack != ACK_OK) {
    Serial.println("   -> FAILED. Power-cycle the ESC and retry; check GP16 + common ground.");
    return;
  }
  uint16_t sig = ((uint16_t)buf[6] << 8) | buf[5];
  Serial.printf("   sig=0x%04X  interfaceMode=%u (%s)\n", sig, buf[8],
                (buf[8] == imSIL_BLB) ? "imSIL_BLB / BLHeli_S" :
                (buf[8] == imARM_BLB) ? "imARM_BLB / BLHeli_32-AM32" : "other");

  // esc-configurator reads 0x70 bytes at 0x1A00 — the BLHeli_S settings block.
  Serial.println("-- 2. cmd_DeviceRead(addr=0x1A00, len=0x70) --");
  uint8_t p1 = 0x70;
  ack = run4Way(buf, cmd_DeviceRead, 0x1A, 0x00, &p1, 1, &olen);
  Serial.printf("   ACK = 0x%02X (%s), %u bytes\n", ack, ackName(ack), olen);
  if (ack != ACK_OK || olen < 0x70) {
    Serial.println("   -> FAILED. This is the read esc-configurator needs; it will report");
    Serial.println("      \"failed reading ESC\" until this succeeds.");
    return;
  }

  const uint8_t *s = &buf[5];              // settings block, offset 0 == flash 0x1A00
  printField("LAYOUT", s + 0x40, 16);      // e.g. "#A_H_05#"
  printField("MCU",    s + 0x50, 16);
  printField("NAME",   s + 0x60, 16);

  // Derive the Bluejay hex name. Middle letter is the MCU: H = BB2/EFM8BB21.
  char layout[17] = {0};
  uint8_t li = 0;
  for (uint8_t i = 0; i < 16 && li < 16; i++) {
    uint8_t c = s[0x40 + i];
    if (c == 0xFF || c == 0x00) break;
    if (c == '#') continue;                // strip the "#...#" wrapper
    layout[li++] = (c == '-') ? '_' : (char)c;
  }
  Serial.printf("\n   >> Bluejay hex for this ESC: %s_<PWM>_v0.21.0.hex   (PWM = 24 / 48 / 96)\n",
                layout[0] ? layout : "<layout unreadable>");
  Serial.println("      From https://github.com/bird-sanctuary/bluejay/releases (v0.21.0).");
  Serial.println("      esc-configurator picks this automatically — do NOT override the layout.");
  Serial.println("\n-- SELF-TEST PASSED: the 4-way protocol layer works. --");
}

void setup() {
  Serial.begin(115200);
  delay(500);
  InitSerialOutput();
  printHelp();
}

void loop() {
  if (Serial.available()) {
    char c = (char)Serial.read();
    switch (c) {
      case 'b': cmdBitTiming(); break;
      case 'r': cmdRawMonitor(); break;
      case 't': cmdKeepAlive(); break;
      case 'i': cmdBootInit(); break;
      case 'f': cmdFullSelfTest(); break;
      case 'h': printHelp(); break;
      default: break;
    }
  }
}

#endif // ESC4WAY_DIAG_MODE
