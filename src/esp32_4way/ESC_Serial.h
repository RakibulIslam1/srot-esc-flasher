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

// PREFER THIS over either overload above. It takes the array BY REFERENCE, so the
// compiler supplies the cap from the buffer's own type and the two numbers can
// never be transposed.
//
// The hazard is real and has already cost one bug. At a call site the 2-arg form
// reads as if the number were the buffer size:
//
//     uint8_t RX_Buf[250] = {0};
//     RX_Size = GetESC(RX_Buf, 250);      // <-- 250 is the TIMEOUT, not the cap
//
// It happens to work only because the default cap is also 250. Enlarge the buffer
// and "update" that 250, and you have changed the timeout while the cap silently
// stays at 250 -- which is exactly the truncation this file's own comment records
// at cmd_DeviceRead ("the old 250-byte cap silently truncated it and corrupted
// every full-page read"). Reading past the buffer is the same mistake in the other
// direction, and that one is a stack overflow.
//
// With the template the cap is not a number anyone types:
//
//     GetESCInto(RX_Buf, 250);            // cap = sizeof(RX_Buf), always
template <size_t N>
inline uint16_t GetESCInto(uint8_t (&rx_buf)[N], uint16_t wait_ms) {
  return GetESC(rx_buf, wait_ms, (uint16_t)N);
}
