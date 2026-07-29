#include <Arduino.h>
#include "Global.h"     // Global variables
#include "4Way.h"       // 4Way defines
#include "ESC_Serial.h" // ESC Serial Code

//#define _DEBUG_       // BlHeli,AM32 4Way Commands

// Which ESC family we're talking to. Set from the device signature at DeviceInitFlash
// (see classifyEsc) and reported to the host in the DeviceInitFlash reply. Several
// commands below branch on it — most importantly the flash PAGE SIZE in PageErase.
static uint8_t CurrentInterfaceMode = imARM_BLB;

// Classify the ESC from its 16-bit device signature, exactly as Betaflight's
// serial_4way.c Connect() does:
//   SiLabs (BLHeli_S / Bluejay, e.g. 0xE8B2 = EFM8BB21x) : 0xE800 < sig < 0xF900
//   Atmel  (BLHeli AVR)                                  : a short explicit list
//   ARM    (BLHeli_32 / AM32)                            : sig_hi 0x01..0x8F, sig_lo == 0x06
static uint8_t classifyEsc(uint8_t sig_hi, uint8_t sig_lo) {
  uint16_t sig = ((uint16_t)sig_hi << 8) | sig_lo;
  if (sig > 0xE800 && sig < 0xF900) return imSIL_BLB;
  if (sig == 0x9307 || sig == 0x930A || sig == 0x930F || sig == 0x940B) return imATM_BLB;
  if (sig_hi > 0x00 && sig_hi < 0x90 && sig_lo == 0x06) return imARM_BLB;
  return imSIL_BLB;   // unknown: SiLabs is the safe default (512-byte pages)
}

uint16_t Check_4Way(uint8_t buf[]) {
  uint8_t cmd = buf[1];
  uint8_t addr_high = buf[2];
  uint8_t addr_low = buf[3];
  uint8_t I_Param_Len = buf[4];
  uint8_t param = buf[5];               // param = ParamBuf[0]
  uint8_t ParamBuf[256] = {0};          // Parameter Buffer
  uint16_t crc = 0;
  uint16_t buf_size;                    // return Output Buffer Size -> O_Param_Len + Header + CRC
  uint8_t ack_out = ACK_OK;
  uint16_t O_Param_Len = 0;

  for (uint8_t i = 0; i < 5 ; i++) {            // CRC Check of Header (CMD, Adress, Size
    crc = _crc_xmodem_update (crc, buf[i]);
  }
  uint8_t InBuff = I_Param_Len;                 // I_Param_Len = 0 -> 256Bytes
  uint16_t i = 0;                               // work counter
  do {                                          // CRC Check of Input Parameter Buffer
    crc = _crc_xmodem_update (crc, buf[i + 5]);
    ParamBuf[i] = buf[i + 5];
    i++;
    InBuff--;
  } while (InBuff != 0);
  uint16_t crc_in = ((buf[i + 5] << 8) | buf[i + 6]);

#ifdef _DEBUG_
  Serial.print("4Way CMD: ");
  Serial.print(cmd, HEX);
  Serial.print(" Adress: ");
  Serial.print(addr_high, HEX);
  Serial.print(addr_low, HEX);
  Serial.print(" ParamBuf Size: ");
  Serial.print(I_Param_Len, HEX);
  // buffer
  Serial.print(" ParamBuf[0]: ");
  Serial.print(ParamBuf[0], HEX);
  // buffer
  Serial.print(" CRC_in: ");
  Serial.print(crc_in, HEX);
  Serial.print(" CRC calculated: ");
  Serial.println(crc, HEX);
#endif

  if (crc_in != crc) {
#ifdef _DEBUG_
    Serial.print("Wrong CRC ");
#endif
    buf[0] = cmd_Remote_Escape;
    buf[1] = cmd;
    buf[2] = addr_high;
    buf[3] = addr_low;
    O_Param_Len = 0x01;
    buf[4] = 0x01;        // Output Param Lenght
    buf[5] = 0x00;        // Dummy
    ack_out = ACK_I_INVALID_CRC;
    buf[6] = ACK_I_INVALID_CRC;    // ACK
    for (uint8_t i = 0; i < 7; i++) {
      crc = _crc_xmodem_update (crc, buf[i]);
    }
    buf[7] = (crc >> 8) & 0xff;
    buf[8] = crc & 0xff;
#ifdef _DEBUG_
    Serial.print("with CRC: 0x");
    Serial.print(buf[7], HEX);
    Serial.print(" ");
    Serial.print(buf[8], HEX);
    Serial.print(" ");
#endif
    buf_size = 9;
    if (cmd < 0x50) {
      return buf_size;
    }
  }

  crc = 0;
  ack_out = ACK_OK;
  buf[5] = 0;

  if (cmd == cmd_DeviceInitFlash) {
#ifdef _DEBUG_
    Serial.print("DeviceInitFlash ");
#endif
    O_Param_Len = 0x04;
    if (param < NUM_ESC) {  // param = ESC index → route to its pin
      selectEsc(param);
      uint8_t BootInit[] = {0, 0, 0, 0, 0, 0, 0, 0, 0x0D, 'B', 'L', 'H', 'e', 'l', 'i', 0xF4, 0x7D};
      uint8_t Init_Size = 17;
      uint16_t RX_Size = 0;   // uint8_t here previously underflowed to RX_Buf[255] on a 0-byte reply
      uint8_t RX_Buf[250] = {0};
      // DeviceInitFlash hat die CRC bereits im Array enthalten, daher darf keine CRC gesendet werden
      RX_Size = SendESC(BootInit, Init_Size, false);        // send without CRC
      //RX_Size = SendESC(BootInit, Init_Size);             // send with CRC
      // NEVER delay() between SendESC and GetESC: the bit-bang receiver is unbuffered
      // and polling-only, so anything the ESC sends while we sleep is lost forever.
      // The wait is expressed as GetESC's first-byte timeout instead.
      // read Answer Format = BootMsg("471c") SIGNATURE_001, SIGNATURE_002, BootVersion, BootPages (,ACK = brSUCCESS = 0x30)
      RX_Size = GetESC(RX_Buf, 250);
#ifdef _DEBUG_
      Serial.print("ESC Received: ");
      Serial.print(RX_Size);
      Serial.print(" ");
      Serial.print("Bytes: ");
      Serial.print(RX_Buf[0], HEX);
      Serial.print(" ");
      Serial.print(RX_Buf[1], HEX);
      Serial.print(" ");
      Serial.print(RX_Buf[2], HEX);
      Serial.print(" ");
      Serial.print(RX_Buf[3], HEX);
      Serial.print(" ");
      Serial.print(RX_Buf[4], HEX);
      Serial.print(" ");
      Serial.print(RX_Buf[5], HEX);
      Serial.print(" ");
      Serial.print(RX_Buf[6], HEX);
      Serial.print(" ");
      Serial.print(RX_Buf[7], HEX);
      Serial.print(" ");
      Serial.print(RX_Buf[8], HEX);
#endif
      // A valid reply is: BootMsg "471x" + SIGNATURE_001 + SIGNATURE_002 + BootVersion
      // + BootPages + ACK(brSUCCESS). Validate the "471" prefix like BL_ConnectEx() does —
      // without it a short/garbage reply silently yields junk signature bytes. (The 4th
      // char is the bootloader revision, NOT a platform flag: a SiLabs BLHeli_S part
      // legitimately answers "471d".)
      bool bootMsgOk = (RX_Size >= 9) &&
                       (RX_Buf[0] == '4') && (RX_Buf[1] == '7') && (RX_Buf[2] == '1');
      if (bootMsgOk && RX_Buf[RX_Size - 1] == brSUCCESS) {
        CurrentInterfaceMode = classifyEsc(RX_Buf[4], RX_Buf[5]);
        buf[5] = RX_Buf[5];       // Device Signature2 (sig lo)
        buf[6] = RX_Buf[4];       // Device Signature1 (sig hi)
        buf[7] = RX_Buf[3];       // bootloader revision char ('c'/'d')
        buf[8] = CurrentInterfaceMode;   // host picks its ESC family code path from this
        buf[9] = ACK_OK;          // ACK
#ifdef _DEBUG_
        Serial.print("OK");
#endif
      }
      else {
        // No usable reply. These are placeholder bytes only — the ACK below is what tells
        // the host it failed; don't assert a family we never actually identified.
        buf[5] = 0x06;        // Device Signature2 (placeholder)
        buf[6] = 0x33;        // Device Signature1 (placeholder)
        buf[7] = 0x67;        // bootloader revision char (placeholder)
        buf[8] = CurrentInterfaceMode;
        ack_out = ACK_D_GENERAL_ERROR;
        buf[9] = ACK_D_GENERAL_ERROR;    // ACK
#ifdef _DEBUG_
        Serial.print("General Error");
#endif
      }
    }
    else {
      ack_out = ACK_I_INVALID_CHANNEL;
      buf[9] = ACK_I_INVALID_CHANNEL;    // ACK
#ifdef _DEBUG_
      Serial.print("Invalid channel");
#endif
    }
  }

  else if (cmd == cmd_DeviceReset) {
#ifdef _DEBUG_
    Serial.print("DeviceReset ");
#endif
    O_Param_Len = 0x01;
    if (param < NUM_ESC) {  // param = ESC index → route to its pin
      selectEsc(param);
      if (Enable4Way) {
        buf[6] = ACK_OK;    // ACK
        uint8_t ESC_data[2] = {RestartBootloader, 0};
        uint16_t Data_Size = 2;
        uint16_t RX_Size = 0;
        RX_Size = SendESC(ESC_data, Data_Size);
        // The RestartBootloader command above IS the real bootloader-entry mechanism:
        // it's a software command the ESC's currently-running APP firmware receives
        // over this same one-wire link and acts on by jumping to its own bootloader.
        // (A signal-line low/high "reset" pulse only works on ESC/board designs that
        // hardware-wire the signal pin to MCU reset — not a generic BLHeli_S property —
        // so we don't rely on that; just give the app firmware a moment to jump.)
        // This delay is INTENTIONAL and safe: RestartBootloader gets no reply, so
        // there is nothing to miss. Do not copy it to the read paths.
        delay(30);
        // Keine Antwort vom ESC -> trotzdem serial leeren
        uint8_t RX_Buf[5] = {0};
        RX_Size = GetESC(RX_Buf, 50, sizeof(RX_Buf));   // cap = buffer size, or it overflows
      }
      else {
        ack_out = ACK_D_GENERAL_ERROR;
        buf[6] = ACK_D_GENERAL_ERROR;
#ifdef _DEBUG_
        Serial.print("4Way not aktive");
#endif
      }
    }
    else {
      buf[5] = 0x00;
      ack_out = ACK_I_INVALID_CHANNEL;
      buf[6] = ACK_I_INVALID_CHANNEL;    // ACK
#ifdef _DEBUG_
      Serial.print("Invalid channel");
#endif
    }
  }

  else if (cmd == cmd_InterfaceTestAlive) {
#ifdef _DEBUG_
    Serial.print("InterfaceTestAlive ");
#endif
    O_Param_Len = 0x01;
    uint8_t ESC_data[2] = {CMD_KEEP_ALIVE, 0};
    uint16_t Data_Size = 2;
    uint16_t RX_Size = 0;
    uint8_t RX_Buf[250] = {0};
    RX_Size = SendESC(ESC_data, Data_Size);
    RX_Size = GetESC(RX_Buf, 20);   // no delay() first — see cmd_DeviceInitFlash
#ifdef _DEBUG_
    Serial.print("ESC Received: ");
    Serial.print(RX_Size);
    Serial.print(" Bytes: ");
    Serial.print(RX_Buf[0], HEX);
#endif
    if (RX_Size != 0 && RX_Buf[0] == brSUCCESS) {
      //buf[6] = ACK_OK;    // ACK
    }
    else {
      //buf[6] = ACK_D_GENERAL_ERROR;    // ACK
    }
  }

  else if (cmd == cmd_DeviceRead) {
#ifdef _DEBUG_
    Serial.print("DeviceRead @ Adress ");
    Serial.print(addr_high, HEX);
    Serial.println(addr_low, HEX);
#endif
    uint8_t ESC_data[4] = {CMD_SET_ADDRESS, 0x00, addr_high, addr_low};
    uint16_t Data_Size = 4;
    uint16_t RX_Size = 0;
    uint8_t RX_Buf[300] = {0};
    uint16_t esc_rx_crc = 0;
    RX_Size = SendESC(ESC_data, Data_Size);
    RX_Size = GetESC(RX_Buf, 20);   // no delay() first — see cmd_DeviceInitFlash
#ifdef _DEBUG_
    Serial.print("Set Adress -> ESC Received: ");
    Serial.print(RX_Size);
    Serial.print(" ACK: ");
    Serial.print(RX_Buf[0], HEX);
#endif
    if (RX_Size != 0 && RX_Buf[0] == brSUCCESS) {
#ifdef _DEBUG_
      Serial.println(" -> Success");
#endif
      // alles ok
      ESC_data[0] = CMD_READ_FLASH_SIL;
      ESC_data[1] = param;
      Data_Size = 2;
      RX_Size = SendESC(ESC_data, Data_Size);
      O_Param_Len = (param == 0) ? 256 : param;
      // No delay() here either — the old delay(param) (delay(112) for the 0x1A00
      // settings read) slept through the ESC's entire reply. GetESC listens from now
      // and reads the contiguous stream until ~2 ms of silence.
      // A full 256-byte page read answers with 259 bytes (256 data + ACK + 2 CRC) — the
      // old 250-byte cap silently truncated it and corrupted every full-page read.
      RX_Size = GetESC(RX_Buf, 50, 260);
#ifdef _DEBUG_
      Serial.print("Read Flash -> ESC Received: ");
      Serial.print(RX_Size);
      Serial.print(" ACK: ");
      Serial.print(RX_Size ? RX_Buf[(RX_Size - 1)] : 0, HEX);   // guard the underflow
#endif
      // Need at least ACK + 2 CRC bytes; RX_Size < 3 would underflow the subtraction
      // below to ~65534 and then loop copying garbage into the response.
      if (RX_Size >= 3) {
        if (RX_Buf[(RX_Size - 1)] == brSUCCESS) {
#ifdef _DEBUG_
          Serial.println(" -> Success");
#endif
        }
        else {
#ifdef _DEBUG_
          Serial.println(" -> Fail");
#endif
          ack_out = ACK_D_GENERAL_ERROR;
        }
        RX_Size = RX_Size - 3;                              // CRC High, CRC_Low and ACK from ESC
        O_Param_Len = RX_Size;
        for (uint16_t i = 5; i < (RX_Size + 5); i++) {
          buf[i] = RX_Buf[i - 5]; // buf[5] = RX_Buf[0]
          esc_rx_crc = ByteCrc(buf[i], esc_rx_crc);
          //Data_Size = i;
        }
        esc_rx_crc = ByteCrc(RX_Buf[(RX_Size)], esc_rx_crc);
        esc_rx_crc = ByteCrc(RX_Buf[(RX_Size + 1)], esc_rx_crc);
#ifdef _DEBUG_
        Serial.print("ESC CRC: : ");
        Serial.print(esc_rx_crc, HEX);
#endif
        if (esc_rx_crc == 0) {
#ifdef _DEBUG_
          Serial.println("-> CRC OK");
#endif
        }
        else {
          ack_out = ACK_D_GENERAL_ERROR;
          O_Param_Len = 0x01;
#ifdef _DEBUG_
          Serial.println("-> CRC Fail");
#endif
        }
      }
      else {
#ifdef _DEBUG_
        Serial.println(" -> Fail");
#endif
        ack_out = ACK_D_GENERAL_ERROR;
        O_Param_Len = 0x01;
      }
    }
    else {
#ifdef _DEBUG_
      Serial.println(" -> Fail");
#endif
      // nix ok
      O_Param_Len = 0x01;
      ack_out = ACK_D_GENERAL_ERROR;
    }
  }

  else if (cmd == cmd_InterfaceExit) {
#ifdef _DEBUG_
    Serial.print("Interface Exit ");
#endif
    DeinitSerialOutput();           // initialisiert PPM IN/OUT
    O_Param_Len = 0x01;
  }

  else if (cmd == cmd_ProtocolGetVersion) {
#ifdef _DEBUG_
    Serial.print("ProtocolGetVersion ");
#endif
    O_Param_Len = 0x01;
    buf[5] = SERIAL_4WAY_PROTOCOL_VER;//0x6C;
  }

  else if (cmd == cmd_InterfaceGetName) {
#ifdef _DEBUG_
    Serial.print("InterfaceGetName ");
#endif
    // SERIAL_4WAY_INTERFACE_NAME_STR "m4wFCIntf"
    O_Param_Len = 0x09;
    //buf[4] = 0x09;        // Output Param Lenght
    buf[5] = 'm';//0x6D;
    buf[6] = '4';//0x34;
    buf[7] = 'w';//0x77;
    buf[8] = 'F';//0x46;
    buf[9] = 'C';//0x43;
    buf[10] = 'I';//0x49;
    buf[11] = 'n';//0x6E;
    buf[12] = 't';//0x74;
    buf[13] = 'f';//0x66;
  }

  else if (cmd == cmd_InterfaceGetVersion) {
#ifdef _DEBUG_
    Serial.print("InterfaceGetVersion ");
#endif
    O_Param_Len = 0x02;
    buf[5] = SERIAL_4WAY_VERSION_HI;//0xC8;
    buf[6] = SERIAL_4WAY_VERSION_LO;//0x04;
  }

  else if (cmd == cmd_InterfaceSetMode) {
#ifdef _DEBUG_
    Serial.print("InterfaceSetMode ");
#endif
    O_Param_Len = 0x01;
    // Accept every family we can actually drive (previously ARM only, which rejected a
    // SiLabs BLHeli_S host with ACK_I_INVALID_PARAM). Same set as Betaflight.
    if (param == imSIL_BLB || param == imATM_BLB || param == imARM_BLB) {
      CurrentInterfaceMode = param;
      //buf[6] = ACK_OK;      // ACK
    }
    else {
      buf[6] = ACK_I_INVALID_PARAM;      // ACK
      ack_out = ACK_I_INVALID_PARAM;
    }
  }

  else if (cmd == cmd_DeviceVerify) {
#ifdef _DEBUG_
    Serial.print("DeviceVerify ");
#endif
    O_Param_Len = 0x01;
    // Verify-in-the-bootloader is an ARM-only command. This used to return ACK_OK without
    // talking to the ESC at all — a fake "verify passed". Report it as unsupported so the
    // host read-back-verifies instead (what Betaflight does, and what SiLabs needs).
    if (CurrentInterfaceMode != imARM_BLB) {
      ack_out = ACK_I_INVALID_CMD;
    }
  }

  else if (cmd == cmd_DevicePageErase) {
#ifdef _DEBUG_
    Serial.print("DevicePageErase ");
#endif
    O_Param_Len = 0x01;
    // Flash page size differs by family: ARM (BLHeli_32/AM32) = 1024 bytes, SiLabs
    // (BLHeli_S/Bluejay, e.g. EFM8BB21x) = 512 bytes. Using the ARM stride on a SiLabs
    // part erases at 2x spacing and wipes the WRONG regions. Same branch as Betaflight.
    addr_high = (CurrentInterfaceMode == imARM_BLB) ? (param << 2) : (param << 1);
    addr_low = 0;
    // Send CMD Adress
    uint8_t ESC_data[4] = {CMD_SET_ADDRESS, 0, addr_high, addr_low};
    uint16_t Data_Size = 4;
    uint16_t RX_Size = 0;
    uint8_t RX_Buf[250] = {0};
    RX_Size = SendESC(ESC_data, Data_Size);
    RX_Size = GetESC(RX_Buf, 20);   // no delay() first — see cmd_DeviceInitFlash
#ifdef _DEBUG_
    Serial.print("ESC Received: ");
    Serial.print(RX_Size);
    Serial.print(" Bytes: ");
    Serial.print(RX_Buf[0], HEX);
#endif
    if (RX_Size != 0 && RX_Buf[0] == brSUCCESS) {
      //buf[6] = ACK_OK;    // ACK
    }
    else {
      ack_out = ACK_D_GENERAL_ERROR;
      buf[6] = ACK_D_GENERAL_ERROR;    // ACK
    }
    // Send Data
    ESC_data[0] = CMD_ERASE_FLASH;
    ESC_data[1] = 0x01;
    Data_Size = 2;
    RX_Size = 0;
    RX_Size = SendESC(ESC_data, Data_Size);
    // Erase is slow (typically ~30 ms). Betaflight allows up to 3 s, but esc-configurator
    // gives the whole 4-way command only 1 s, so cap the wait well inside that.
    RX_Size = GetESC(RX_Buf, 600);
#ifdef _DEBUG_
    Serial.print("ESC Received: ");
    Serial.print(RX_Size);
    Serial.print(" Bytes: ");
    Serial.print(RX_Buf[0], HEX);
#endif
    if (RX_Size != 0 && RX_Buf[0] == brSUCCESS) {
      //buf[6] = ACK_OK;    // ACK
    }
    else {
      ack_out = ACK_D_GENERAL_ERROR;
      buf[6] = ACK_D_GENERAL_ERROR;    // ACK
    }
  }

  else if (cmd == cmd_DeviceWrite) {
#ifdef _DEBUG_
    Serial.print("DeviceWrite ");
#endif
    O_Param_Len = 0x01;
    // Send CMD Adress
    uint8_t ESC_data[4] = {CMD_SET_ADDRESS, 0, addr_high, addr_low};
    uint16_t Data_Size = 4;
    uint16_t RX_Size = 0;
    uint8_t RX_Buf[250] = {0};
    RX_Size = SendESC(ESC_data, Data_Size);
    RX_Size = GetESC(RX_Buf, 20);   // no delay() first — see cmd_DeviceInitFlash
#ifdef _DEBUG_
    Serial.print("ESC Received: ");
    Serial.print(RX_Size);
    Serial.print(" Bytes: ");
    Serial.print(RX_Buf[0], HEX);
#endif
    if (RX_Size != 0 && RX_Buf[0] == brSUCCESS) {

    }
    else {
      ack_out = ACK_D_GENERAL_ERROR;
    }
    // sende Buffer init
    ESC_data[0] = CMD_SET_BUFFER;
    ESC_data[1] = 0x00;
    ESC_data[2] = 0x00;
    ESC_data[3] = I_Param_Len;
    Data_Size = 4;
    RX_Size = 0;
    if (I_Param_Len == 0) {
      ESC_data[2] = 0x01;
    }
    RX_Size = SendESC(ESC_data, Data_Size);
    // Keine Anwort vom ESC — CMD_SET_BUFFER deliberately does not ACK before its
    // payload, so there is intentionally no GetESC() here.

    // sende Buffer data
    RX_Size = SendESC(ParamBuf, I_Param_Len);
    RX_Size = GetESC(RX_Buf, 150);   // no delay() first — see cmd_DeviceInitFlash
#ifdef _DEBUG_
    Serial.print("ESC Received: ");
    Serial.print(RX_Size);
    Serial.print(" Bytes: ");
    Serial.print(RX_Buf[0], HEX);
#endif
    if (RX_Size != 0 && RX_Buf[0] == brSUCCESS) {

    }
    else {
      ack_out = ACK_D_GENERAL_ERROR;
    }

    // sende write CMD
    ESC_data[0] = CMD_PROG_FLASH;
    ESC_data[1] = 0x01;
    Data_Size = 2;
    RX_Size = 0;
    RX_Size = SendESC(ESC_data, Data_Size);
    // brSUCCESS wird nicht sofort gesendet — flash programming takes a while, so wait
    // for it via the timeout rather than delay()ing through the reply.
    RX_Size = GetESC(RX_Buf, 600);
#ifdef _DEBUG_
    Serial.print("ESC Received: ");
    Serial.print(RX_Size);
    Serial.print(" Bytes: ");
    Serial.print(RX_Buf[0], HEX);
#endif
    if (RX_Size != 0 && RX_Buf[0] == brSUCCESS) {
      //buf[6] = ACK_OK;    // ACK
    }
    else {
      ack_out = ACK_D_GENERAL_ERROR;
      //buf[6] = ACK_D_GENERAL_ERROR;    // ACK
    }
  }

  else {
    // Unimplemented (ReadEEprom/WriteEEprom/EraseAll/C2CK_LOW) or unknown command. Setting
    // buf_size here is pointless — the common tail below overwrites it — so these used to
    // answer ACK_OK having done nothing. Say so honestly instead. (BLHeli_S keeps its
    // settings in flash ~0x1A00, read via cmd_DeviceRead, so the EEPROM ops aren't needed.)
    O_Param_Len = 0x01;
    ack_out = ACK_I_INVALID_CMD;
#ifdef _DEBUG2_
    Serial.print("else command: ");
    Serial.print(cmd, HEX);
#endif
  }

  crc = 0;
  buf[0] = cmd_Remote_Escape;
  buf[1] = cmd;
  buf[2] = addr_high;
  buf[3] = addr_low;
  buf[4] = O_Param_Len & 0xff;        // Output Param Lenght
  buf[O_Param_Len + 5] = ack_out;
  // CRC
  for (uint16_t i = 0; i < (O_Param_Len + 6); i++) {
    crc = _crc_xmodem_update (crc, buf[i]);
  }
  buf[O_Param_Len + 6] = (crc >> 8) & 0xff;
  buf[O_Param_Len + 7] = crc & 0xff;
#ifdef _DEBUG_
  Serial.print(" with CRC: 0x");
  Serial.print(buf[O_Param_Len + 6], HEX);
  Serial.print(" ");
  Serial.print(buf[O_Param_Len + 7], HEX);
  Serial.print(" ");
#endif
  buf_size = (O_Param_Len + 8);

  return buf_size;
}

uint16_t _crc_xmodem_update (uint16_t crc, uint8_t data) {
  int i;

  crc = crc ^ ((uint16_t)data << 8);
  for (i = 0; i < 8; i++) {
    if (crc & 0x8000)
      crc = (crc << 1) ^ 0x1021;
    else
      crc <<= 1;
  }
  return crc;
}
