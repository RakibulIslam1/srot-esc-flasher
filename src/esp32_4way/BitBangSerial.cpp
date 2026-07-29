#include "BitBangSerial.h"

// Guards the timing-critical portion of one byte (~520us) against FreeRTOS
// preemption / other-core activity shifting a sample point. Never held across
// the "wait for the next byte" loop, which can legitimately run for hundreds of
// ms — that stays a plain poll so the watchdog/scheduler are never starved.
static portMUX_TYPE s_bbMux = portMUX_INITIALIZER_UNLOCKED;

static inline void bbWaitUntil(uint32_t t0, uint32_t offset_us) {
  while ((uint32_t)(micros() - t0) < offset_us) { /* busy wait */ }
}

void bbInit(uint8_t pin) {
  pinMode(pin, INPUT_PULLUP);
}

void bbSetOutput(uint8_t pin) {
  digitalWrite(pin, HIGH);   // set the level before switching direction (no glitch)
  pinMode(pin, OUTPUT);
  digitalWrite(pin, HIGH);   // idle-high
}

void bbSetInput(uint8_t pin) {
  pinMode(pin, INPUT_PULLUP);
}

void bbWriteByte(uint8_t pin, uint8_t b) {
  portENTER_CRITICAL(&s_bbMux);
  uint32_t t0 = micros();
  digitalWrite(pin, LOW);                                     // start bit
  bbWaitUntil(t0, BB_BIT_TIME_US);
  for (uint8_t i = 0; i < 8; i++) {
    digitalWrite(pin, (b >> i) & 0x01);                        // LSB first
    bbWaitUntil(t0, (uint32_t)(i + 2) * BB_BIT_TIME_US);
  }
  digitalWrite(pin, HIGH);                                    // stop bit
  bbWaitUntil(t0, 10u * BB_BIT_TIME_US);                       // hold a full stop-bit period
  portEXIT_CRITICAL(&s_bbMux);
}

int16_t bbReadByte(uint8_t pin, uint32_t timeout_us) {
  uint32_t t0 = micros();
  // Tight poll for the start edge — deliberately NO yield() here. A FreeRTOS task
  // switch at this exact moment stamps `edge` up to a whole bit-time late, which
  // shifts every subsequent sample by one bit: a leading '4' (0x34) then decodes as
  // 0x9A and still passes the stop-bit check, so it corrupts silently. Only the first
  // byte of a reply is exposed (later bytes arrive back-to-back, so the poll exits
  // immediately). Bounded by timeout_us (<= 600 ms in this firmware), far below the
  // 5 s task watchdog, so briefly starving the idle task is safe. Do not add yield().
  while (digitalRead(pin)) {
    if ((uint32_t)(micros() - t0) >= timeout_us) return -1;    // no start edge in time
  }
  uint32_t edge = micros();

  portENTER_CRITICAL(&s_bbMux);
  // Sample each bit at its CENTRE: start bit at +0.5 bit, data bit i at +(i+1.5)
  // bits, stop bit at +9.5 bits — a full +/-26 us of margin on either side.
  bbWaitUntil(edge, BB_HALF_BIT_US);
  if (digitalRead(pin) != LOW) {                                // glitch, not a real start bit
    portEXIT_CRITICAL(&s_bbMux);
    return -1;
  }

  uint8_t value = 0;
  for (uint8_t i = 0; i < 8; i++) {
    bbWaitUntil(edge, BB_HALF_BIT_US + (uint32_t)(i + 1) * BB_BIT_TIME_US);
    if (digitalRead(pin)) value |= (uint8_t)(1u << i);          // LSB first
  }
  bbWaitUntil(edge, BB_HALF_BIT_US + 9u * BB_BIT_TIME_US);      // stop bit
  bool stopOk = digitalRead(pin) != 0;
  portEXIT_CRITICAL(&s_bbMux);

  return stopOk ? (int16_t)value : (int16_t)-1;
}
