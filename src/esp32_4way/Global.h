
// 4 ESC signal pins on the ESP32 DevKit V1 (single-wire half-duplex each).
#define NUM_ESC 4
extern const uint8_t ESC_PIN[NUM_ESC];
extern uint8_t g_cur_pin;      // the currently-selected ESC pin (set by selectEsc)

extern uint8_t serial_rx[300];

extern bool Enable4Way;
