#include <Arduino.h>
#include "Global.h"

// ESC1..4 signal → these GPIOs (safe on the WROOM DevKit V1). One wire each — the
// interface uses single-pin half-duplex, so NO resistor is needed.
const uint8_t ESC_PIN[NUM_ESC] = {16, 17, 18, 19};
uint8_t g_cur_pin = 16;

uint8_t serial_rx[300] = {0};

bool Enable4Way = false;
