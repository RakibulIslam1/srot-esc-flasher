void InitSerialOutput(void);
void DeinitSerialOutput(void);
void selectEsc(uint8_t idx);   // route to ESC_PIN[idx]
void escResetPulse(void);      // reset-to-bootloader pulse on the current ESC pin
uint16_t SendESC(uint8_t tx_buf[], uint16_t buf_size, bool CRC);
uint16_t SendESC(uint8_t tx_buf[], uint16_t buf_size);
uint16_t ByteCrc(uint8_t data, uint16_t crc);
// max_len MUST NOT exceed the caller's buffer — GetESC keeps reading until the ESC goes
// quiet, so a small buffer with a large cap is a stack overflow. Note a full 256-byte
// flash read replies with 259 bytes (256 data + ACK + 2 CRC), so pass >= 260 there.
uint16_t GetESC(uint8_t rx_buf[], uint16_t wait_ms, uint16_t max_len);
uint16_t GetESC(uint8_t rx_buf[], uint16_t wait_ms );   // defaults to max_len = 250
