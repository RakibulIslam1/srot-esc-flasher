#include <Arduino.h>
#include "Global.h"           // globale variablen
#include "serial_comm.h"      // Header
#include "MSP.h"              // MSP process
#include "4Way.h"             // 4way process

uint16_t serial_rx_counter = 0;
uint16_t serial_tx_counter = 0;
uint16_t serial_buffer_len = 0;

bool serial_command = false;

void process_serial(void) {
  if (Serial.available()) {
    delay(10);
    serial_command = true;
    while (Serial.available()) {
      serial_rx[serial_rx_counter] = Serial.read();
      //Serial.print(serial_rx[serial_rx_counter],HEX);                      // mit serieller Anzeige was gesendet wurde
      //Serial.print(" ");

      if ((serial_rx_counter == 4) && (serial_rx[0] == 0x2F)) {
        // 4 Way Command: Size (0 = 256) + 7 Byte Overhead
        if (serial_rx[4] == 0) {
          serial_buffer_len = 256 + 7;
        }
        else {
          serial_buffer_len = serial_rx[4] + 7;
        }
      }
      if ((serial_rx_counter == 3) && (serial_rx[0] == 0x24)) {
        // MSP Command: Size (0 = 0)+ 6 Byte Overhead
        serial_buffer_len = serial_rx[3] + 6;
      }

      serial_rx_counter ++;
      if (serial_rx_counter == serial_buffer_len) {
        break;
      }
      // Never run past the buffer. A frame whose length byte we never saw (bad header, or
      // a stale serial_buffer_len from a previous frame) would otherwise keep appending
      // past serial_rx[] forever. Drop the frame and resync instead.
      if (serial_rx_counter >= sizeof(serial_rx)) {
        serial_rx_counter = 0;
        serial_buffer_len = 0;
        serial_command = false;
        break;
      }
    }
  }

  if (serial_command) {
    if ((serial_rx[0] == 0x2F) && (serial_rx_counter == serial_buffer_len)) {
      // 4 Way Command
      // 4 Way proceed
      serial_tx_counter = Check_4Way(serial_rx);
      for (uint16_t b = 0; b < serial_tx_counter; b++) {
        Serial.write(serial_rx[b]);
      }
      serial_command = false;
      serial_rx_counter = 0;
      serial_tx_counter = 0;
    }
    else if (serial_rx[0] == 0x24 && serial_rx[1] == 0x4D && serial_rx[2] == 0x3C) {
      // MSP Command
      // MSP Proceed
      serial_tx_counter = MSP_Check(serial_rx, serial_rx_counter);
      //Serial.print("length ");
      //Serial.println(i);
      for (uint8_t b = 0; b < serial_tx_counter; b++) {
        Serial.write(serial_rx[b]);
      }
      serial_command = false;
      serial_rx_counter = 0;
      serial_tx_counter = 0;
    }
    else if (serial_rx[0] != 0x2F && serial_rx[0] != 0x24) {
      // Neither a 4-Way nor an MSP frame — resync rather than latching serial_command
      // true forever with a stale length (which then just accumulates bytes).
      serial_command = false;
      serial_rx_counter = 0;
      serial_buffer_len = 0;
    }
  }
}
