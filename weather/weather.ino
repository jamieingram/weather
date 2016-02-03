#define SHORT_PULSE  500
#define LONG_PULSE  1500
#define SHORT_MARGIN 300
#define LONG_MARGIN 300

int val = 0;
unsigned long transition_t = micros();
unsigned long now, duration;

#ifdef DEBUG
unsigned long short_min = 9999, short_max = 0;
unsigned long long_min = 9999, long_max = 0;
unsigned int pulse_buffer[200];
unsigned int pb_idx = 0;
#endif

#define BUFFER_SIZE  16

byte byte_buffer[BUFFER_SIZE];
byte buffer_idx = 0;

byte sig_seen = 0;

unsigned short shift_register = 0;
byte bit_count = 0;

/*
* Function taken from Luc Small (http://lucsmall.com), itself
* derived from the OneWire Arduino library. Modifications to
* the polynomial according to Fine Offset's CRC8 calulations.
*/
uint8_t _crc8( uint8_t *addr, uint8_t len)
{
        uint8_t crc = 0;

        // Indicated changes are from reference CRC-8 function in OneWire library
        while (len--) {
                uint8_t inbyte = *addr++;
                uint8_t i;
                for (i = 8; i; i--) {
                        uint8_t mix = (crc ^ inbyte) & 0x80; // changed from & 0x01
                        crc <<= 1; // changed from right shift
                        if (mix) crc ^= 0x31;// changed from 0x8C;
                        inbyte <<= 1; // changed from right shift
                }
        }
        return crc;
}

void setup() {

  // put your setup code here, to run once:
  pinMode(2, INPUT);

  Serial.begin(9600);  // Debugging only

  // prints title with ending line break
  Serial.println("WH1080RF Monitor");
}

void loop() {

  // put your main code here, to run repeatedly:

  int newVal;
  for(int i=0; i < 10; i++) {
    newVal += digitalRead(2) ? 1 : 0;
    delayMicroseconds(5);
  }
  newVal = (newVal + 5) / 10;

  /*
   * Handle situations where the clock has rolled over
   * between transitions (happens every ~70 mins).
   */
  now = micros();

  if(transition_t <= now)
    duration = now - transition_t;
  else
    duration = (~transition_t) + now;

  if(newVal != val) {  // then transitioning state

    /*
     *  We update the transition time for the pulse, and
     *  change the current state of the input value.
     */
    transition_t = now;
    val = newVal;

    /*
     *  If the pulse width (hi or low) is outside the
     *  range of the Fine Offset signal, then ignore them.
     */
    if(duration < (SHORT_PULSE - SHORT_MARGIN) 
        || duration > (LONG_PULSE + LONG_MARGIN)) {
      // Meaningless pulse
      return;
    }

    /*
     *  If we reach here, then we have seen a potentially
     *  valid pulse. Shift the bit into the register.
     */
    if(newVal == 1) {
      // rising edge of a pulse (0 -> 1)
    } else {
      // falling edge of a pulse (1 -> 0)
      if( duration >= (SHORT_PULSE - SHORT_MARGIN) && duration <= (SHORT_PULSE + SHORT_MARGIN) ) {
        // short pulse is binary '1'
        shift_register = (shift_register << 1) | 0x01;
        bit_count++;

        #ifdef DEBUG
        if(duration < short_min) short_min = duration;
        if(duration > short_max) short_max = duration;
        if(pb_idx < 200) pulse_buffer[pb_idx++] = duration;
        #endif

      } else if(duration >= (LONG_PULSE - LONG_MARGIN) && duration <= (LONG_PULSE + LONG_MARGIN)) {
        // long pulse is binary '0'
        shift_register = (shift_register << 1);
        bit_count++;

        #ifdef DEBUG
        if(duration < long_min) long_min = duration;
        if(duration > long_max) long_max = duration;
        if(pb_idx < 200) pulse_buffer[pb_idx++] = duration;
        #endif
      }
    }

    // Look for signature of 0xfa (4 bits 0xf0 pre-amble + 0xa)
    if((shift_register & 0xff) == 0xfa && buffer_idx == 0) {
      // Found signature - discard pre-amble and leave 0x0a.
      shift_register = 0x0a;
      bit_count = 4;
      sig_seen = 1;  // Flag that the signature has been seen.

      #ifdef DEBUG
      pb_idx = 0;
      #endif

    } else if(bit_count == 8 && sig_seen) {
      // Got a byte, so store it if we have room.
      if(buffer_idx < BUFFER_SIZE)
        byte_buffer[buffer_idx++] = (byte)(shift_register & 0xff);
      else
        Serial.println("Overflow on byte");
      shift_register = 0;
      bit_count = 0;
    }

  } else {

      /*
       *  Have we reached timeout on duration? If so, process any
       *  bytes present in the buffer and then reset the state
       *  variables.
       */    
      if(duration > 5000) {

        if (buffer_idx > 0) {

          /*
           *  Dump the bytes to the Serial port.
           */
          Serial.print("Found ");
          Serial.println(buffer_idx);

          for(int i = 0; i < buffer_idx; i++) {
            for (byte mask = 0x80; mask; mask >>= 1) {
              Serial.print(mask & byte_buffer[i] ? '1' : '0');
            }
            Serial.print(" ");
            Serial.println(byte_buffer[i]);
          }

          /*
           *  If we have enough bytes, then verify the checksum.
           */
          if(buffer_idx >= 10 && _crc8(byte_buffer, 9) == byte_buffer[9]) {

            Serial.println("CRC Passed");
          }

          buffer_idx = 0;
        }

        #ifdef DEBUG
          for(int i = 0; i < pb_idx; i++) {
            Serial.print("Pulse ");
            Serial.print(i);
            Serial.print(" ");
            Serial.println(pulse_buffer[i]);
          }

          Serial.print("Short ");
          Serial.print(short_min);
          Serial.print(" ");
          Serial.println(short_max);

          Serial.print("Long ");
          Serial.print(long_min);
          Serial.print(" ");
          Serial.println(long_max);

          short_min = long_min = 9999;
          short_max = long_max = 0;

        #endif

        shift_register = 0;
        bit_count = 0;
        sig_seen = 0;
      }

      // No transition on this iteration
      // Serial.flush();
  }
}  // end loop()
