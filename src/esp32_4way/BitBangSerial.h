#pragma once
#include <Arduino.h>

// Single-pin half-duplex bit-bang UART for the BLHeli 4-way ESC link — modeled
// directly on Betaflight/INAV's own serial_4way_avrootloader.c: 19200 baud,
// LSB-first, 8N1, idle-high via INPUT_PULLUP, TX drives push-pull OUTPUT. One
// pin, no resistor — this is literally how a real flight controller does it.
//
// Hardware UART / EspSoftwareSerial were tried first and both have documented
// ESP32-specific bugs when TX and RX share one GPIO (see docs/ESC_FLASHING.md).
// This driver never asks the peripheral to share a pin — it explicitly switches
// the pin's MODE (input vs. output) around each direction change instead.

#define BB_BIT_TIME_US   52u   // 19200 baud
#define BB_HALF_BIT_US   26u   // half a bit — every bit is sampled at its centre

void bbInit(uint8_t pin);                 // idle the pin: INPUT_PULLUP
void bbSetOutput(uint8_t pin);            // switch to OUTPUT push-pull, drive idle-high
void bbSetInput(uint8_t pin);             // switch back to INPUT_PULLUP (idle/RX state)
void bbWriteByte(uint8_t pin, uint8_t b); // pin must already be OUTPUT (bbSetOutput)

// Returns 0-255 for a clean byte, or -1 if no start edge arrived within timeout_us,
// or on a framing error (stop bit not high). Pin must already be INPUT (bbSetInput).
int16_t bbReadByte(uint8_t pin, uint32_t timeout_us);
