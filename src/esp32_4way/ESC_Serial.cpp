#include <Arduino.h>
#include "Global.h"             // Global variables
#include "ESC_Serial.h"         // ESC Serial Header
#include "BitBangSerial.h"      // single-pin half-duplex bit-bang UART

//#define _DEBUG_

// ---------------------------------------------------------------------------
// One-wire (single signal wire) link to the ESC bootloader — a hand-written
// single-pin bit-bang half-duplex UART (BitBangSerial.*), modeled directly on
// Betaflight/INAV's own serial_4way_avrootloader.c: the pin's MODE is switched
// between INPUT_PULLUP (idle/RX) and push-pull OUTPUT (TX) around each direction
// change. No resistor, no shared-pin hardware UART, no EspSoftwareSerial — those
// all have documented ESP32-specific bugs (see docs/ESC_FLASHING.md).
// ---------------------------------------------------------------------------

uint16_t esc_crc = 0;

void InitSerialOutput() {
  Enable4Way = true;
  for (uint8_t i = 0; i < NUM_ESC; i++) bbInit(ESC_PIN[i]);   // all 4 wires idle
  g_cur_pin = ESC_PIN[0];                                     // default to ESC 1
#ifdef _DEBUG_
  Serial.println("Init ESC Serial (bit-bang one-wire)");
#endif
}

// Route to one of the 4 ESC pins (called by the 4-Way DeviceInitFlash/Reset with the
// ESC index). The 4 wires are independent GPIOs — no shared state to re-attach.
void selectEsc(uint8_t idx) {
  if (idx >= NUM_ESC) return;
  g_cur_pin = ESC_PIN[idx];
}

void DeinitSerialOutput() {
  Enable4Way = false;
}

// Best-effort reset-to-bootloader pulse. Only helps ESC/board designs that wire the
// signal line to a hardware reset (most standalone BLHeli_S ESCs don't) — the real
// bootloader entry is the RestartBootloader command sent over the (now-working) one-
// wire link, see 4Way.cpp's cmd_DeviceReset. Kept for reference; not called by default.
void escResetPulse() {
  uint8_t pin = g_cur_pin;
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
  delay(300);
  digitalWrite(pin, HIGH);
  delay(2);
  bbInit(pin);   // back to idle INPUT_PULLUP
}

static uint16_t sendFrame(uint8_t tx_buf[], uint16_t buf_size, bool appendCrc) {
  esc_crc = 0;
  if (buf_size == 0) buf_size = 256;
  for (uint16_t i = 0; i < buf_size; i++) {
    esc_crc = ByteCrc(tx_buf[i], esc_crc);
  }

  bbSetOutput(g_cur_pin);
  for (uint16_t i = 0; i < buf_size; i++) {
    bbWriteByte(g_cur_pin, tx_buf[i]);
  }
  if (appendCrc) {
    bbWriteByte(g_cur_pin, (uint8_t)(esc_crc & 0xff));
    bbWriteByte(g_cur_pin, (uint8_t)((esc_crc >> 8) & 0xff));
  }
  bbSetInput(g_cur_pin);   // release the line so the ESC can drive its reply
#ifdef _DEBUG_
  Serial.println("SendESC done");
#endif
  return 0;
}

uint16_t SendESC(uint8_t tx_buf[], uint16_t buf_size) {
  return sendFrame(tx_buf, buf_size, true);
}

uint16_t SendESC(uint8_t tx_buf[], uint16_t buf_size, bool CRC) {
  return sendFrame(tx_buf, buf_size, CRC);
}

// Wait up to wait_ms for the ESC's reply, then read the whole frame (until ~2ms of
// silence) into at most max_len bytes. Returns the number of bytes received.
uint16_t GetESC(uint8_t rx_buf[], uint16_t wait_ms, uint16_t max_len) {
  uint16_t total = 0;
  if (max_len == 0) return 0;
  int16_t b = bbReadByte(g_cur_pin, (uint32_t)wait_ms * 1000u);
  if (b < 0) return 0;                       // nothing arrived at all
  rx_buf[total++] = (uint8_t)b;

  while (total < max_len) {
    b = bbReadByte(g_cur_pin, 2000u);        // ~2ms idle ends the frame
    if (b < 0) break;
    rx_buf[total++] = (uint8_t)b;
  }
#ifdef _DEBUG_
  Serial.print("ESC Read: ");
  for (uint16_t i = 0; i < total; i++) { Serial.print(rx_buf[i], HEX); Serial.print(' '); }
  Serial.println("done");
#endif
  return total;
}

uint16_t GetESC(uint8_t rx_buf[], uint16_t wait_ms) {
  return GetESC(rx_buf, wait_ms, 250);
}

uint16_t ByteCrc(uint8_t data, uint16_t crc) {
  uint8_t xb = data;
  for (uint8_t i = 0; i < 8; i++) {
    if (((xb & 0x01) ^ (crc & 0x0001)) != 0 ) {
      crc = crc >> 1;
      crc = crc ^ 0xA001;
    } else {
      crc = crc >> 1;
    }
    xb = xb >> 1;
  }
  return crc;
}
