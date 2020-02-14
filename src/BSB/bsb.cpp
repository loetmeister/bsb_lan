#if defined(ARDUINO) && ARDUINO >= 100
#include "Arduino.h"
#else
#include "WProgram.h"
#endif

#include "bsb.h"

//#define DEBUG_LL 1

// Constructor
BSB::BSB(uint8_t rx, uint8_t tx, uint8_t addr, uint8_t d_addr) {
  bus_type = 0;
  len_idx = 3;
  myAddr=addr;
  destAddr=d_addr;
  rx_pin=rx;

#if defined(__SAM3X8E__)
  pinMode(53, OUTPUT);    // provide voltage
  digitalWrite(53, 1);
  pinMode(24, OUTPUT);    // provide 3V3 volt also via pin 24 for V2 versions of PCB board when used on the Due. Cut the 5V pin, short the 5V hole to pin 24 (via pin 22) to get necessary 3V3 voltage.
  digitalWrite(24, 1);
#endif
  if (rx == 19) {	// 19 = RX pin of Serial1 USART module
    HwSerial = true;
    serial = &Serial1;
  } else {
#if !defined(__SAM3X8E__)
    BSBSoftwareSerial* serial_sw = new BSBSoftwareSerial(rx, tx, true);
    serial = serial_sw;
    serial_sw->begin(4800);
    serial_sw->listen();
#endif
  }
}

void BSB::enableInterface() {
  if (HwSerial == true) {	// 19 = RX pin of Serial1 USART module
    Serial1.begin(4800, SERIAL_8O1);
  }
}

uint8_t BSB::setBusType(uint8_t bus_type_val, uint16_t addr, uint16_t d_addr) {
  bus_type = bus_type_val;
  switch (bus_type) {
    case 0:
      len_idx = 3;
      pl_start = 9;
      break;
    case 1:
      len_idx = 1;
      pl_start = 13;
      break;
    case 2:
      len_idx = 8;
      pl_start = 6;
      break;
    default: len_idx = 3; pl_start = 9; break;
  }
  if (addr<=0xff) {
    myAddr = addr & 0xFF;
  }
  if (d_addr<=0xff) {
    destAddr = d_addr & 0xFF;
  }
  Serial.print(F("My address: "));
  Serial.println(myAddr);
  Serial.print(F("Destination address: "));
  Serial.println(destAddr);
  
  return bus_type;
}

uint8_t BSB::getBusType() {
  return bus_type;
}

uint8_t BSB::getBusAddr() {
  return myAddr;
}

uint8_t BSB::getBusDest() {
  return destAddr;
}

uint8_t BSB::getLen_idx() {
  if (bus_type == 2) {
    return len_idx +1;  // different only for BUS_PPS
  }
  return len_idx;
}

uint8_t BSB::getPl_start() {
  return pl_start;
}


// Dumps a message to Serial
void BSB::print(byte* msg) {
  if (bus_type != 2) {
    byte len = msg[len_idx];
    if (len > 32) return;
    byte data = 0;

    for (int i=0; i<len+bus_type; i++) {	// msg length counts from zero with LPB (bus_type 1) and from 1 with BSB (bus_type 0)
      data = msg[i];
      if (data < 16) Serial.print("0");
      Serial.print(data, HEX);
      Serial.print(" ");
    }
    Serial.println();
  }
}

// Receives a message and stores it to buffer
boolean BSB::Monitor(byte* msg) {
  unsigned long int ts;
  byte i=0;
    
  if (serial->available() > 0) {
    // get timestamp
    ts=millis();
    // output
    Serial.print(ts);
    Serial.print(" ");
    while (serial->available() > 0) {
      // Read serial data...
      msg[i] = readByte();;

      // output
      if(msg[i]<16){  
        Serial.print("0");
      }
      Serial.print(msg[i], HEX);
      Serial.print(" ");
      i++;
      // if no inout available -> wait
      if (serial->available() == 0) {
        unsigned long timeout = millis() + 3;// > ((11/4800)*1000);   // Interestingly, here the timeout is already set to 3ms... (see GetMessage() below)
        while (millis() < timeout) {
          delayMicroseconds(15);                                      // ...but unclear to me (FH) why the delay is done in 15us steps when nothing else is done after each iteration...
        }
      }
      // if still no input available telegramm has finished
      if (serial->available() == 0) break;
    }
    Serial.println();
    return true;
  }
  return false;
}

bool BSB::GetMessage(byte* msg) {
  byte i=0,timeout;
  uint8_t read;

  while (serial->available() > 0) {
    // Read serial data...
    read = readByte();

#if DEBUG_LL
    Serial.println();    
    if(read<16){  
      Serial.print("0");
    }
    Serial.print(read, HEX);
    Serial.print(" ");
#endif    
    
    // ... until SOF detected (= 0xDC, 0xDE bei BSB bzw. 0x78 bei LPB)
    if ((bus_type == 0 && (read == 0xDC || read == 0xDE)) || (bus_type == 1 && read == 0x78) || (bus_type == 2 && (read == 0x17 || read == 0x1D || read == 0x1E))) {
      // Restore otherwise dropped SOF indicator
      msg[i++] = read;
      if (bus_type == 2 && read == 0x17) {
      	uint8_t PPS_write_enabled = myAddr;
      	if (PPS_write_enabled == 1) {
          return true; // PPS-Bus request byte 0x17 just contains one byte, so return
      	} else {
      	  len_idx = 9;
	      }
      }

      // Delay for more data
      if (HwSerial == true) {
        delay(4);   // I wonder why HardwareSerial needs longer than SoftwareSerial until a character is ready to be processed. Also, why 3ms are fine for the Mega, but at least 4ms are necessary on the Due
      } else {
        delay(1);   // Or should I wonder why SoftwareSerial is fine with just 1ms? 
                    // At 4800bps 8O1, one byte needs 11 Bit to be transferred. One bit takes 0.2ms transmit time. Thus, 11 bits
                    // take 2.2ms, and therefore, obviously, a new byte can only appear after 2.2ms.
                    // The question is if serial.available() reacts differently in SoftwareSerial and HardwareSerial - maybe
                    // SoftwareSerial reacts as soon as a new bit comes in, and HardwareSerial only notifies once a full byte is ready?
      }
      // read the rest of the message
      while (serial->available() > 0) {
        read = readByte();
        msg[i++] = read;
#if DEBUG_LL
        if(read<16){  
          Serial.print("0");
        }
        Serial.print(read, HEX);
        Serial.print(" ");
#endif
        // Break if message seems to be completely received (i==msg.length)
        if (i > len_idx) {
          if (bus_type == 2) {
            break;
          }
          if ( msg[len_idx] > 32 ) // check for maximum message length
            break;
          if (i >= msg[len_idx]+bus_type)
            break;
        }
        // Delay until we got next byte
        if (serial->available() == 0) {
          if (HwSerial == true) {
            timeout = 200;  // again, see above, why does HwSerial take more time to process a character? Here, timeout easily counts down 120 times 15 microseconds for a new character to be ready to process... 
          } else {
            timeout = 30;
          }
          while ((timeout > 0) && (serial->available() == 0)){
            delayMicroseconds(15);
            timeout--;
          }
        }

        // Break if next byte signals next message (0x23 ^ 0xFF == 0xDC)
        // if((serial->peek() == 0x23)) break;
        // DO NOT break because some messages contain a 0xDC 
      }

      // We should have read the message completely. Now check and return

      if (bus_type == 2) {
        if (i == len_idx+1) {
	        len_idx = 8;
          return true; // TODO: add CRC check before returning true/false
        }
      } else {

        if (i == msg[len_idx]+bus_type) {		// LPB msg length is one less than BSB
          // Seems to have received all data
          if (bus_type == 1) {
            if (CRC_LPB(msg, i-1)-msg[i-2]*256-msg[i-1] == 0) return true;
            else return false;
	        } else {
            if (CRC(msg, i) == 0) return true;
            else return false;
	        }
        } else {
          // Length error
          return false;
        }
      }
    }
  }
  // We got no data so:
  return false;
}

// Generates CCITT XMODEM CRC from BSB message
uint16_t BSB::CRC (byte* buffer, uint8_t length) {
  uint16_t crc = 0;

  for (uint8_t i = 0; i < length; i++) {
    crc = _crc_xmodem_update(crc, buffer[i]);
  }

  // Complete message returns 0x00
  // Message w/o last 2 bytes (CRC) returns last 2 bytes (CRC)
  return crc;
}

// Generates checksum from LPB message
// (255 - (Telegrammlänge ohne PS - 1)) * 256 + Telegrammlänge ohne PS - 1 + Summe aller Telegrammbytes
uint16_t BSB::CRC_LPB (byte* buffer, uint8_t length) {
  uint16_t crc = 0;

  crc = (257-length)*256+length-2;

  for (uint8_t i = 0; i < length-1; i++) {
    crc = crc+buffer[i];
  }

  return crc;
}

// Generates CRC for PPS message
uint8_t BSB::CRC_PPS (byte* buffer, uint8_t length) {
  uint8_t crc = 0;
  int sum = 0;

  for (uint8_t i = 0; i < length; i++) {
    sum+=buffer[i];
  }
  sum = sum & 0xFF;
  crc = 0xFF - sum + 1;

  return crc;
}

uint16_t BSB::_crc_xmodem_update (uint16_t crc, uint8_t data) {
  uint8_t i;

  crc = crc ^ ((uint16_t)data << 8);
  for (i=0; i<8; i++) {
    if (crc & 0x8000) {
      crc = (crc << 1) ^ 0x1021;
    } else {
      crc <<= 1;
    }
  }
  return crc;
}

// Low-Level sending of message to bus
inline bool BSB::_send(byte* msg) {
// Nun - Ein Teilnehmer will senden :
  byte data, len;
  if (bus_type != 2) {
    len = msg[len_idx];
  } else {
    len = len_idx;
  }
  // switch (bus_type) {
    // case 0:
      // msg[0] = 0xDC;
      // msg[1] = myAddr | 0x80;
      // msg[2] = destAddr;
      // break;
    // case 1:
      // msg[0] = 0x78;
      // msg[2] = destAddr;
      // msg[3] = myAddr;
      // break;
  // }
  {
    if (bus_type == 0) {
      msg[0] = 0xDC;
      msg[1] = myAddr | 0x80;
      msg[2] = destAddr;
      uint16_t crc = CRC (msg, len -2);
      msg[len -2] = (crc >> 8);
      msg[len -1] = (crc & 0xFF);
    }
    if (bus_type == 1) {
      msg[0] = 0x78;
      msg[2] = destAddr;
      msg[3] = myAddr;
      uint16_t crc = CRC_LPB (msg, len);
      msg[len-1] = (crc >> 8);
      msg[len] = (crc & 0xFF);
    }
    if (bus_type == 2) {
      uint8_t crc = CRC_PPS (msg, len);
      msg[len] = crc;
    }
  }

#if DEBUG_LL  
  print(msg);
#endif  
  /*
Er wartet 11/4800 Sek ab (statt 10, Hinweis von miwi), lauscht und schaut ob der Bus in dieser Zeit von jemand anderem benutzt wird. Sprich ob der Bus in dieser Zeit mal
auf 0 runtergezogen wurde. Wenn ja - mit den warten neu anfangen.
*/
  static const unsigned long timeoutabort = 1000;  // one second timeout
  unsigned long start_timer = millis();
  retry:
  // Select a random wait time between 60 and 79 ms
  unsigned long waitfree = random(1,60) + 25; // range 26 .. 85 ms
//  unsigned long waitfree = random(1,20) + 59; // range 60 .. 79 ms
  { // block begins
    if(millis()-start_timer > timeoutabort){  // one second has elapsed
      return false;
    }
    if (bus_type != 2) {
      // Wait 59 ms plus a random time
      unsigned long timeout = millis();
//      unsigned long timeout = millis() + 3;//((1/480)*1000);
      while (millis()-timeout < waitfree) {
//        if ((HwSerial == true && rx_pin_read() == false) || (HwSerial == false && rx_pin_read()))   // Test RX pin
      if (rx_pin_read())
        {
          goto retry;
        } // endif
      } // endwhile
    }
  } // block ends

  //Serial.println("bus free");

/*
Wenn nicht wird das erste Bit gesendet. ( Startbit )

Jedes gesendete Bit wird ( wegen Bus ) ja sofort auf der Empfangsleitung
wieder ankommen. Man schaut nach, ob das gesendete Bit mit dem
empfangenen Bit übereinstimmt.
Wenn ich eine "0" sende - also den Bus auf High lasse, dann will ich
sehen, dass der Bus weiterhin auf High ist. Sollte ein anderer
Teilnehmer in dieser Zeit eine "1" senden - also den Bus herunterziehen,
dann höre ich sofort auf mit dem Senden und fange oben wieder an.
Danach folgen nach gleichem Muster die folgenden Bits, Bit 7..0, Parity
und Stop Bit.
*/

/* 
FH 27.12.2018: Wer auch immer das obige geschrieben hat, es macht bezogen auf
den nachfolgenden Code keinen Sinn: 
1. Es wird hier nicht bitweise gesendet, sondern ein ganzes Byte an
BSBSoftwareSerial::write übergeben. Dort wird dann unabhängig davon, ob der 
Bus frei ist oder nicht, dieses komplette Byte inkl. Start-, Stop- und Parity-
Bits gesendet.
2. BSBSoftwareSerial::write gibt immer 1 zurück, außer wenn _tx_delay == 0 ist.
Diese Variable wird aber nur einmalig bei Aufruf von BSBSoftwareSerial::begin
gesetzt und wäre nur in seltenen Ausnahmefällen == 0.
So wie es jetzt scheint, findet die Kollisionsprüfung beim Senden nicht statt.
*/

#if defined(__AVR__)
  if (HwSerial == false) {
    cli();
  }
#endif
  byte loop_len = len;
  if (bus_type != 2) {
    loop_len = len + bus_type - 1; // same msg length difference as above
  }
  for (byte i=0; i <= loop_len; i++) {
    data = msg[i];
    if (bus_type != 2) {
      data = data ^ 0xFF;
    }
    serial->write(data);
    if (HwSerial == true) {
      serial->flush();
      readByte(); // Read (and discard) the byte that was just sent so that it isn't processed as an incoming message
    }
//    if ((HwSerial == true && rx_pin_read() == false) || (HwSerial == false && rx_pin_read())) {  // Test RX pin (logical 1 is 0 with HardwareSerial and 1 with SoftwareSerial inverted)
      if (rx_pin_read()) {
      // Collision
#if defined(__AVR__)
      if (HwSerial == false) {
        sei();
      }
#endif
      goto retry;
    }
  }
  if (HwSerial == true) {
    serial->flush();
  } else {
#if defined(__AVR__)
    sei();
#endif
  }
  return true;
}

bool BSB::Send(uint8_t type, uint32_t cmd, byte* rx_msg, byte* tx_msg, byte* param, byte param_len, bool wait_for_reply) {
  byte i;
  byte offset = 0;

  if (bus_type == 2) {
    return _send(tx_msg);
  }

  if (HwSerial == true) {
    while(serial->available()) {
      readByte();
    }
  }

  // first two bytes are swapped
  byte A2 = (cmd & 0xff000000) >> 24;
  byte A1 = (cmd & 0x00ff0000) >> 16;
  byte A3 = (cmd & 0x0000ff00) >> 8;
  byte A4 = (cmd & 0x000000ff);

  // special treatment of internal query types
  if (type == 0x12) {   // TYPE_IA1
    A1 = A3;
    A2 = A4;
    offset = 2;
  }
  if (type == 0x14) {   // TYPE_IA2
    A1 = A4;
    offset = 3;
  }
  
  if (bus_type == 1) {
    tx_msg[1] = param_len + 14 - offset;
    tx_msg[4] = 0xC0;	// some kind of sending/receiving flag?
    tx_msg[5] = 0x02;	// yet unknown
    tx_msg[6] = 0x00;	// yet unknown
    tx_msg[7] = 0x14;	// yet unknown
    tx_msg[8] = type;
    // Adress
    tx_msg[9] = A1;
    tx_msg[10] = A2;
    tx_msg[11] = A3;
    tx_msg[12] = A4;
  } else {
    tx_msg[3] = param_len + 11 - offset;
    tx_msg[4] = type;
    // Adress
    tx_msg[5] = A1;
    tx_msg[6] = A2;
    tx_msg[7] = A3;
    tx_msg[8] = A4;
  }

  // Value
  for (i=0; i < param_len; i++) {
    if (bus_type == 1) {
      tx_msg[13+i] = param[i];
    } else {
      tx_msg[9+i] = param[i];
    }
  }
  if(!_send(tx_msg)) return false;
  if(!wait_for_reply) return true;

  i=15;

  unsigned long timeout = millis() + 3000;
  while ((i > 0) && (millis() < timeout)) {
    if (GetMessage(rx_msg)) {
#if DEBUG_LL
      Serial.print(F("Duration until answer received: "));
      Serial.println(3000-(timeout-millis()));
#endif
      i--;
      byte msg_type = rx_msg[4+(bus_type*4)];
      if (msg_type == 0x13 || msg_type == 0x15) {   // no recipient check for answers for special query types 0x12/0x13 and 0x14/0x15
        return true;
      }
      if (bus_type == 1) {
/* Activate for LPB systems with truncated error messages (no commandID in return telegram) 
	if (rx_msg[2] == myAddr && rx_msg[8]==0x08) {  // TYPE_ERR
	  return false;
	}
*/
        if (rx_msg[2] == myAddr && rx_msg[9] == A2 && rx_msg[10] == A1 && rx_msg[11] == A3 && rx_msg[12] == A4) {
          return true;
	      } else {
#if DEBUG_LL
          Serial.println(F("Message received, but not for us:"));
          print(rx_msg);
#endif
        }
      } else {
        if ((rx_msg[2] == myAddr) && (rx_msg[5] == A2) && (rx_msg[6] == A1) && (rx_msg[7] == A3) && (rx_msg[8] == A4)) {
          return true;
	      } else {
#if DEBUG_LL
          Serial.println(F("Message received, but not for us:"));
          print(rx_msg);
#endif
        }
      }
    }
    else {
      delayMicroseconds(205);
    }
  }
#if DEBUG_LL
  Serial.println(F("No answer for this send telegram:"));
#endif
  print(tx_msg);

  return false;
}

boolean BSB::rx_pin_read() {
  return boolean(* portInputRegister(digitalPinToPort(rx_pin)) & digitalPinToBitMask(rx_pin)) ^ HwSerial;
}

uint8_t BSB::readByte() {
  byte read = serial->read();
  if (bus_type != 2) {
    read = read ^ 0xFF;
  }
  return read;
}