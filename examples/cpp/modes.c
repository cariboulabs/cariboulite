/*
 * Source of file: https://github.com/watson/libmodes
 * Author: Thomas Watson (@watson)
 * Contact: w@tson.dk / https://twitter.com/wa7son
 * License: BSD-2-Clause
 */

#include "modes.h"

#define MODE_S_PREAMBLE_US 8       // microseconds
#define MODE_S_LONG_MSG_BITS 112
#define MODE_S_SHORT_MSG_BITS 56
#define MODE_S_FULL_LEN (MODE_S_PREAMBLE_US+MODE_S_LONG_MSG_BITS)

#define MODE_S_ICAO_CACHE_TTL 60   // Time to live of cached addresses.

static uint16_t maglut[129*129*2];
static int maglut_initialized = 0;

// =============================== Initialization ===========================

void mode_s_init(mode_s_t *self) {
  int i, q;

  self->fix_errors = 1;
  self->check_crc = 1;
  self->aggressive = 0;

  // Allocate the ICAO address cache. We use two uint32_t for every entry
  // because it's a addr / timestamp pair for every entry
  memset(&self->icao_cache, 0, sizeof(self->icao_cache));

  // Populate the I/Q -> Magnitude lookup table. It is used because sqrt or
  // round may be expensive and may vary a lot depending on the libc used.
  //
  // We scale to 0-255 range multiplying by 1.4 in order to ensure that every
  // different I/Q pair will result in a different magnitude value, not losing
  // any resolution.
  if (!maglut_initialized) {
    for (i = 0; i <= 128; i++) {
      for (q = 0; q <= 128; q++) {
        maglut[i*129+q] = round(sqrt(i*i+q*q)*360);
      }
    }
    maglut_initialized = 1;
  }
}

// ===================== Mode S detection and decoding  =====================

// Parity table for MODE S Messages.
//
// The table contains 112 elements, every element corresponds to a bit set in
// the message, starting from the first bit of actual data after the preamble.
//
// For messages of 112 bit, the whole table is used. For messages of 56 bits
// only the last 56 elements are used.
// 
// The algorithm is as simple as xoring all the elements in this table for
// which the corresponding bit on the message is set to 1.
// 
// The latest 24 elements in this table are set to 0 as the checksum at the end
// of the message should not affect the computation.
//
// Note: this function can be used with DF11 and DF17, other modes have the CRC
// xored with the sender address as they are reply to interrogations, but a
// casual listener can't split the address from the checksum.
uint32_t mode_s_checksum_table[] = {
  0x3935ea, 0x1c9af5, 0xf1b77e, 0x78dbbf, 0xc397db, 0x9e31e9, 0xb0e2f0, 0x587178,
  0x2c38bc, 0x161c5e, 0x0b0e2f, 0xfa7d13, 0x82c48d, 0xbe9842, 0x5f4c21, 0xd05c14,
  0x682e0a, 0x341705, 0xe5f186, 0x72f8c3, 0xc68665, 0x9cb936, 0x4e5c9b, 0xd8d449,
  0x939020, 0x49c810, 0x24e408, 0x127204, 0x093902, 0x049c81, 0xfdb444, 0x7eda22,
  0x3f6d11, 0xe04c8c, 0x702646, 0x381323, 0xe3f395, 0x8e03ce, 0x4701e7, 0xdc7af7,
  0x91c77f, 0xb719bb, 0xa476d9, 0xadc168, 0x56e0b4, 0x2b705a, 0x15b82d, 0xf52612,
  0x7a9309, 0xc2b380, 0x6159c0, 0x30ace0, 0x185670, 0x0c2b38, 0x06159c, 0x030ace,
  0x018567, 0xff38b7, 0x80665f, 0xbfc92b, 0xa01e91, 0xaff54c, 0x57faa6, 0x2bfd53,
  0xea04ad, 0x8af852, 0x457c29, 0xdd4410, 0x6ea208, 0x375104, 0x1ba882, 0x0dd441,
  0xf91024, 0x7c8812, 0x3e4409, 0xe0d800, 0x706c00, 0x383600, 0x1c1b00, 0x0e0d80,
  0x0706c0, 0x038360, 0x01c1b0, 0x00e0d8, 0x00706c, 0x003836, 0x001c1b, 0xfff409,
  0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
  0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
  0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000
};

uint32_t mode_s_checksum(unsigned char *msg, int bits) {
  uint32_t crc = 0;
  int offset = (bits == 112) ? 0 : (112-56);
  int j;

  for(j = 0; j < bits; j++) {
    int byte = j/8;
    int bit = j%8;
    int bitmask = 1 << (7-bit);

    // If bit is set, xor with corresponding table entry.
    if (msg[byte] & bitmask)
      crc ^= mode_s_checksum_table[j+offset];
  }
  return crc; // 24 bit checksum.
}

// Given the Downlink Format (DF) of the message, return the message length in
// bits.
int mode_s_msg_len_by_type(int type) {
  if (type == 16 || type == 17 ||
      type == 19 || type == 20 ||
      type == 21)
    return MODE_S_LONG_MSG_BITS;
  else
    return MODE_S_SHORT_MSG_BITS;
}

// Try to fix single bit errors using the checksum. On success modifies the
// original buffer with the fixed version, and returns the position of the
// error bit. Otherwise if fixing failed -1 is returned.
int fix_single_bit_errors(unsigned char *msg, int bits) {
  int j;
  unsigned char aux[MODE_S_LONG_MSG_BITS/8];

  for (j = 0; j < bits; j++) {
    int byte = j/8;
    int bitmask = 1 << (7-(j%8));
    uint32_t crc1, crc2;

    memcpy(aux, msg, bits/8);
    aux[byte] ^= bitmask; // Flip j-th bit.

    crc1 = ((uint32_t)aux[(bits/8)-3] << 16) |
           ((uint32_t)aux[(bits/8)-2] << 8) |
            (uint32_t)aux[(bits/8)-1];
    crc2 = mode_s_checksum(aux, bits);

    if (crc1 == crc2) {
      // The error is fixed. Overwrite the original buffer with the
      // corrected sequence, and returns the error bit position.
      memcpy(msg, aux, bits/8);
      return j;
    }
  }
  return -1;
}

// Similar to fix_single_bit_errors() but try every possible two bit
// combination. This is very slow and should be tried only against DF17
// messages that don't pass the checksum, and only in Aggressive Mode.
int fix_two_bits_errors(unsigned char *msg, int bits) {
  int j, i;
  unsigned char aux[MODE_S_LONG_MSG_BITS/8];

  for (j = 0; j < bits; j++) {
    int byte1 = j/8;
    int bitmask1 = 1 << (7-(j%8));

    // Don't check the same pairs multiple times, so i starts from j+1
    for (i = j+1; i < bits; i++) {
      int byte2 = i/8;
      int bitmask2 = 1 << (7-(i%8));
      uint32_t crc1, crc2;

      memcpy(aux, msg, bits/8);

      aux[byte1] ^= bitmask1; // Flip j-th bit.
      aux[byte2] ^= bitmask2; // Flip i-th bit.

      crc1 = ((uint32_t)aux[(bits/8)-3] << 16) |
             ((uint32_t)aux[(bits/8)-2] << 8) |
              (uint32_t)aux[(bits/8)-1];
      crc2 = mode_s_checksum(aux, bits);

      if (crc1 == crc2) {
        // The error is fixed. Overwrite the original buffer with the
        // corrected sequence, and returns the error bit position.
        memcpy(msg, aux, bits/8);
        // We return the two bits as a 16 bit integer by shifting 'i'
        // on the left. This is possible since 'i' will always be
        // non-zero because i starts from j+1.
        return j | (i<<8);
      }
    }
  }
  return -1;
}

// Hash the ICAO address to index our cache of MODE_S_ICAO_CACHE_LEN elements,
// that is assumed to be a power of two.
uint32_t icao_cache_has_addr(uint32_t a) {
  // The following three rounds wil make sure that every bit affects every
  // output bit with ~ 50% of probability.
  a = ((a >> 16) ^ a) * 0x45d9f3b;
  a = ((a >> 16) ^ a) * 0x45d9f3b;
  a = ((a >> 16) ^ a);
  return a & (MODE_S_ICAO_CACHE_LEN-1);
}

// Add the specified entry to the cache of recently seen ICAO addresses. Note
// that we also add a timestamp so that we can make sure that the entry is only
// valid for MODE_S_ICAO_CACHE_TTL seconds.
void add_recently_seen_icao_addr(mode_s_t *self, uint32_t addr) {
  uint32_t h = icao_cache_has_addr(addr);
  self->icao_cache[h*2] = addr;
  self->icao_cache[h*2+1] = (uint32_t) time(NULL);
}

// Returns 1 if the specified ICAO address was seen in a DF format with proper
// checksum (not xored with address) no more than * MODE_S_ICAO_CACHE_TTL
// seconds ago. Otherwise returns 0.
int icao_addr_was_recently_seen(mode_s_t *self, uint32_t addr) {
  uint32_t h = icao_cache_has_addr(addr);
  uint32_t a = self->icao_cache[h*2];
  int32_t t = self->icao_cache[h*2+1];

  return a && a == addr && time(NULL)-t <= MODE_S_ICAO_CACHE_TTL;
}

// If the message type has the checksum xored with the ICAO address, try to
// brute force it using a list of recently seen ICAO addresses.
//
// Do this in a brute-force fashion by xoring the predicted CRC with the
// address XOR checksum field in the message. This will recover the address: if
// we found it in our cache, we can assume the message is ok.
//
// This function expects mm->msgtype and mm->msgbits to be correctly populated
// by the caller.
//
// On success the correct ICAO address is stored in the mode_s_msg structure in
// the aa3, aa2, and aa1 fiedls.
//
// If the function successfully recovers a message with a correct checksum it
// returns 1. Otherwise 0 is returned.
int brute_force_ap(mode_s_t *self, unsigned char *msg, struct mode_s_msg *mm) {
  unsigned char aux[MODE_S_LONG_MSG_BYTES];
  int msgtype = mm->msgtype;
  int msgbits = mm->msgbits;

  if (msgtype == 0 ||         // Short air surveillance
      msgtype == 4 ||         // Surveillance, altitude reply
      msgtype == 5 ||         // Surveillance, identity reply
      msgtype == 16 ||        // Long Air-Air survillance
      msgtype == 20 ||        // Comm-A, altitude request
      msgtype == 21 ||        // Comm-A, identity request
      msgtype == 24)          // Comm-C ELM
  {
    uint32_t addr;
    uint32_t crc;
    int lastbyte = (msgbits/8)-1;

    // Work on a copy.
    memcpy(aux, msg, msgbits/8);

    // Compute the CRC of the message and XOR it with the AP field so that
    // we recover the address, because:
    //
    // (ADDR xor CRC) xor CRC = ADDR.
    crc = mode_s_checksum(aux, msgbits);
    aux[lastbyte] ^= crc & 0xff;
    aux[lastbyte-1] ^= (crc >> 8) & 0xff;
    aux[lastbyte-2] ^= (crc >> 16) & 0xff;
    
    // If the obtained address exists in our cache we consider the message
    // valid.
    addr = aux[lastbyte] | (aux[lastbyte-1] << 8) | (aux[lastbyte-2] << 16);
    if (icao_addr_was_recently_seen(self, addr)) {
      mm->aa1 = aux[lastbyte-2];
      mm->aa2 = aux[lastbyte-1];
      mm->aa3 = aux[lastbyte];
      return 1;
    }
  }
  return 0;
}

// Decode the 13 bit AC altitude field (in DF 20 and others). Returns the
// altitude, and set 'unit' to either MODE_S_UNIT_METERS or MDOES_UNIT_FEETS.
int decode_ac13_field(unsigned char *msg, int *unit) {
  int m_bit = msg[3] & (1<<6);
  int q_bit = msg[3] & (1<<4);

  if (!m_bit) {
    *unit = MODE_S_UNIT_FEET;
    if (q_bit) {
      // N is the 11 bit integer resulting from the removal of bit Q and M
      int n = ((msg[2]&31)<<6) |
              ((msg[3]&0x80)>>2) |
              ((msg[3]&0x20)>>1) |
               (msg[3]&15);
      // The final altitude is due to the resulting number multiplied by
      // 25, minus 1000.
      return n*25-1000;
    } else {
      // TODO: Implement altitude where Q=0 and M=0
    }
  } else {
    *unit = MODE_S_UNIT_METERS;
    // TODO: Implement altitude when meter unit is selected.
  }
  return 0;
}

// Decode the 12 bit AC altitude field (in DF 17 and others). Returns the
// altitude or 0 if it can't be decoded.
int decode_ac12_field(unsigned char *msg, int *unit) {
  int q_bit = msg[5] & 1;

  if (q_bit) {
    // N is the 11 bit integer resulting from the removal of bit Q
    *unit = MODE_S_UNIT_FEET;
    int n = ((msg[5]>>1)<<4) | ((msg[6]&0xF0) >> 4);
    // The final altitude is due to the resulting number multiplied by 25,
    // minus 1000.
    return n*25-1000;
  } else {
    return 0;
  }
}

static const char *ais_charset = "?ABCDEFGHIJKLMNOPQRSTUVWXYZ????? ???????????????0123456789??????";

// Decode a raw Mode S message demodulated as a stream of bytes by
// mode_s_detect(), and split it into fields populating a mode_s_msg structure.
void mode_s_decode(mode_s_t *self, struct mode_s_msg *mm, unsigned char *msg) {
  uint32_t crc2; // Computed CRC, used to verify the message CRC.

  // Work on our local copy
  memcpy(mm->msg, msg, MODE_S_LONG_MSG_BYTES);
  msg = mm->msg;

  // Get the message type ASAP as other operations depend on this
  mm->msgtype = msg[0]>>3;    // Downlink Format
  mm->msgbits = mode_s_msg_len_by_type(mm->msgtype);

  // CRC is always the last three bytes.
  mm->crc = ((uint32_t)msg[(mm->msgbits/8)-3] << 16) |
            ((uint32_t)msg[(mm->msgbits/8)-2] << 8) |
             (uint32_t)msg[(mm->msgbits/8)-1];
  crc2 = mode_s_checksum(msg, mm->msgbits);

  // Check CRC and fix single bit errors using the CRC when possible (DF 11 and 17).
  mm->errorbit = -1;  // No error
  mm->crcok = (mm->crc == crc2);

  if (!mm->crcok && self->fix_errors && (mm->msgtype == 11 || mm->msgtype == 17)) {
    if ((mm->errorbit = fix_single_bit_errors(msg, mm->msgbits)) != -1) {
        mm->crc = mode_s_checksum(msg, mm->msgbits);
        mm->crcok = 1;
    } else if (self->aggressive && mm->msgtype == 17 &&
               (mm->errorbit = fix_two_bits_errors(msg, mm->msgbits)) != -1) {
      mm->crc = mode_s_checksum(msg, mm->msgbits);
      mm->crcok = 1;
    }
  }

  // Note that most of the other computation happens *after* we fix the
  // single bit errors, otherwise we would need to recompute the fields
  // again.
  mm->ca = msg[0] & 7;        // Responder capabilities.

  // ICAO address
  mm->aa1 = msg[1];
  mm->aa2 = msg[2];
  mm->aa3 = msg[3];

  // DF 17 type (assuming this is a DF17, otherwise not used)
  mm->metype = msg[4] >> 3;   // Extended squitter message type.
  mm->mesub = msg[4] & 7;     // Extended squitter message subtype.

  // Fields for DF4,5,20,21
  mm->fs = msg[0] & 7;        // Flight status for DF4,5,20,21
  mm->dr = msg[1] >> 3 & 31;  // Request extraction of downlink request.
  mm->um = ((msg[1] & 7)<<3)| // Request extraction of downlink request.
            msg[2]>>5;

  // In the squawk (identity) field bits are interleaved like that (message
  // bit 20 to bit 32):
  //
  // C1-A1-C2-A2-C4-A4-ZERO-B1-D1-B2-D2-B4-D4
  //
  // So every group of three bits A, B, C, D represent an integer from 0 to
  // 7.
  //
  // The actual meaning is just 4 octal numbers, but we convert it into a
  // base ten number tha happens to represent the four octal numbers.
  //
  // For more info: http://en.wikipedia.org/wiki/Gillham_code
  {
    int a, b, c, d;

    a = ((msg[3] & 0x80) >> 5) |
        ((msg[2] & 0x02) >> 0) |
        ((msg[2] & 0x08) >> 3);
    b = ((msg[3] & 0x02) << 1) |
        ((msg[3] & 0x08) >> 2) |
        ((msg[3] & 0x20) >> 5);
    c = ((msg[2] & 0x01) << 2) |
        ((msg[2] & 0x04) >> 1) |
        ((msg[2] & 0x10) >> 4);
    d = ((msg[3] & 0x01) << 2) |
        ((msg[3] & 0x04) >> 1) |
        ((msg[3] & 0x10) >> 4);
    mm->identity = a*1000 + b*100 + c*10 + d;
  }

  // DF 11 & 17: try to populate our ICAO addresses whitelist. DFs with an AP
  // field (xored addr and crc), try to decode it.
  if (mm->msgtype != 11 && mm->msgtype != 17) {
    // Check if we can check the checksum for the Downlink Formats where
    // the checksum is xored with the aircraft ICAO address. We try to
    // brute force it using a list of recently seen aircraft addresses.
    if (brute_force_ap(self, msg, mm)) {
      // We recovered the message, mark the checksum as valid.
      mm->crcok = 1;
    } else {
      mm->crcok = 0;
    }
  } else {
    // If this is DF 11 or DF 17 and the checksum was ok, we can add this
    // address to the list of recently seen addresses.
    if (mm->crcok && mm->errorbit == -1) {
      uint32_t addr = (mm->aa1 << 16) | (mm->aa2 << 8) | mm->aa3;
      add_recently_seen_icao_addr(self, addr);
    }
  }

  // Decode 13 bit altitude for DF0, DF4, DF16, DF20
  if (mm->msgtype == 0 || mm->msgtype == 4 ||
      mm->msgtype == 16 || mm->msgtype == 20) {
    mm->altitude = decode_ac13_field(msg, &mm->unit);
  }

  // Decode extended squitter specific stuff.
  if (mm->msgtype == 17) {
    // Decode the extended squitter message.

    if (mm->metype >= 1 && mm->metype <= 4) {
      // Aircraft Identification and Category
      mm->aircraft_type = mm->metype-1;
      mm->flight[0] = (ais_charset)[msg[5]>>2];
      mm->flight[1] = ais_charset[((msg[5]&3)<<4)|(msg[6]>>4)];
      mm->flight[2] = ais_charset[((msg[6]&15)<<2)|(msg[7]>>6)];
      mm->flight[3] = ais_charset[msg[7]&63];
      mm->flight[4] = ais_charset[msg[8]>>2];
      mm->flight[5] = ais_charset[((msg[8]&3)<<4)|(msg[9]>>4)];
      mm->flight[6] = ais_charset[((msg[9]&15)<<2)|(msg[10]>>6)];
      mm->flight[7] = ais_charset[msg[10]&63];
      mm->flight[8] = '\0';
    } else if (mm->metype >= 9 && mm->metype <= 18) {
      // Airborne position Message
      mm->fflag = msg[6] & (1<<2);
      mm->tflag = msg[6] & (1<<3);
      mm->altitude = decode_ac12_field(msg, &mm->unit);
      mm->raw_latitude = ((msg[6] & 3) << 15) |
                          (msg[7] << 7) |
                          (msg[8] >> 1);
      mm->raw_longitude = ((msg[8]&1) << 16) |
                           (msg[9] << 8) |
                           msg[10];
    } else if (mm->metype == 19 && mm->mesub >= 1 && mm->mesub <= 4) {
      // Airborne Velocity Message
      if (mm->mesub == 1 || mm->mesub == 2) {
        mm->ew_dir = (msg[5]&4) >> 2;
        mm->ew_velocity = ((msg[5]&3) << 8) | msg[6];
        mm->ns_dir = (msg[7]&0x80) >> 7;
        mm->ns_velocity = ((msg[7]&0x7f) << 3) | ((msg[8]&0xe0) >> 5);
        mm->vert_rate_source = (msg[8]&0x10) >> 4;
        mm->vert_rate_sign = (msg[8]&0x8) >> 3;
        mm->vert_rate = ((msg[8]&7) << 6) | ((msg[9]&0xfc) >> 2);
        // Compute velocity and angle from the two speed components
        mm->velocity = sqrt(mm->ns_velocity*mm->ns_velocity+
                            mm->ew_velocity*mm->ew_velocity);
        if (mm->velocity) {
          int ewv = mm->ew_velocity;
          int nsv = mm->ns_velocity;
          double heading;

          if (mm->ew_dir) ewv *= -1;
          if (mm->ns_dir) nsv *= -1;
          heading = atan2(ewv, nsv);

          // Convert to degrees.
          mm->heading = heading * 360 / (M_PI*2);
          // We don't want negative values but a 0-360 scale.
          if (mm->heading < 0) mm->heading += 360;
        } else {
          mm->heading = 0;
        }
      } else if (mm->mesub == 3 || mm->mesub == 4) {
        mm->heading_is_valid = msg[5] & (1<<2);
        mm->heading = (360.0/128) * (((msg[5] & 3) << 5) |
                                      (msg[6] >> 3));
      }
    }
  }
  mm->phase_corrected = 0; // Set to 1 by the caller if needed.
}

// Turn I/Q samples pointed by `data` into the magnitude vector pointed by `mag`
void mode_s_compute_magnitude_vector(short *data, uint16_t *mag, uint32_t size)
{
  uint32_t j;

  // Compute the magnitude vector. It's just SQRT(I^2 + Q^2), but we rescale
  // to the 0-255 range to exploit the full resolution.
  /*for (j = 0; j < size; j += 2) {
    int i = data[j]-127;
    int q = data[j+1]-127;

    if (i < 0) i = -i;
    if (q < 0) q = -q;
    mag[j/2] = maglut[i*129+q];
  }*/
	for (j=0; j<size; j+=2)
	{
		int i = data[j];
		int q = data[j+1];
		mag[j/2]=sqrt(i*i + q*q);
	}
}

// Return -1 if the message is out of fase left-side
// Return  1 if the message is out of fase right-size
// Return  0 if the message is not particularly out of phase.
//
// Note: this function will access mag[-1], so the caller should make sure to
// call it only if we are not at the start of the current buffer.
int detect_out_of_phase(uint16_t *mag) {
  if (mag[3] > mag[2]/3) return 1;
  if (mag[10] > mag[9]/3) return 1;
  if (mag[6] > mag[7]/3) return -1;
  if (mag[-1] > mag[1]/3) return -1;
  return 0;
}

// This function does not really correct the phase of the message, it just
// applies a transformation to the first sample representing a given bit:
//
// If the previous bit was one, we amplify it a bit.
// If the previous bit was zero, we decrease it a bit.
//
// This simple transformation makes the message a bit more likely to be
// correctly decoded for out of phase messages:
//
// When messages are out of phase there is more uncertainty in sequences of the
// same bit multiple times, since 11111 will be transmitted as continuously
// altering magnitude (high, low, high, low...)
//
// However because the message is out of phase some part of the high is mixed
// in the low part, so that it is hard to distinguish if it is a zero or a one.
//
// However when the message is out of phase passing from 0 to 1 or from 1 to 0
// happens in a very recognizable way, for instance in the 0 -> 1 transition,
// magnitude goes low, high, high, low, and one of of the two middle samples
// the high will be *very* high as part of the previous or next high signal
// will be mixed there.
//
// Applying our simple transformation we make more likely if the current bit is
// a zero, to detect another zero. Symmetrically if it is a one it will be more
// likely to detect a one because of the transformation. In this way similar
// levels will be interpreted more likely in the correct way.
void apply_phase_correction(uint16_t *mag) {
  int j;

  mag += 16; // Skip preamble.
  for (j = 0; j < (MODE_S_LONG_MSG_BITS-1)*2; j += 2) {
    if (mag[j] > mag[j+1]) {
      // One
      mag[j+2] = (mag[j+2] * 5) / 4;
    } else {
      // Zero
      mag[j+2] = (mag[j+2] * 4) / 5;
    }
  }
}

// Detect a Mode S messages inside the magnitude buffer pointed by 'mag' and of
// size 'maglen' bytes. Every detected Mode S message is convert it into a
// stream of bits and passed to the function to display it.
void mode_s_detect(mode_s_t *self, uint16_t *mag, uint32_t maglen, mode_s_callback_t cb) {
  unsigned char bits[MODE_S_LONG_MSG_BITS];
  unsigned char msg[MODE_S_LONG_MSG_BITS/2];
  uint16_t aux[MODE_S_LONG_MSG_BITS*2];
  uint32_t j;
  int use_correction = 0;

  // The Mode S preamble is made of impulses of 0.5 microseconds at the
  // following time offsets:
  //
  // 0   - 0.5 usec: first impulse.
  // 1.0 - 1.5 usec: second impulse.
  // 3.5 - 4   usec: third impulse.
  // 4.5 - 5   usec: last impulse.
  // 
  // Since we are sampling at 2 Mhz every sample in our magnitude vector is
  // 0.5 usec, so the preamble will look like this, assuming there is an
  // impulse at offset 0 in the array:
  //
  // 0   -----------------
  // 1   -
  // 2   ------------------
  // 3   --
  // 4   -
  // 5   --
  // 6   -
  // 7   ------------------
  // 8   --
  // 9   -------------------
  for (j = 0; j < maglen - MODE_S_FULL_LEN*2; j++) {
    int low, high, delta, i, errors;
    int good_message = 0;

    if (use_correction) goto good_preamble; // We already checked it.

    // First check of relations between the first 10 samples representing a
    // valid preamble. We don't even investigate further if this simple
    // test is not passed.
    if (!(mag[j] > mag[j+1] &&
        mag[j+1] < mag[j+2] &&
        mag[j+2] > mag[j+3] &&
        mag[j+3] < mag[j] &&
        mag[j+4] < mag[j] &&
        mag[j+5] < mag[j] &&
        mag[j+6] < mag[j] &&
        mag[j+7] > mag[j+8] &&
        mag[j+8] < mag[j+9] &&
        mag[j+9] > mag[j+6]))
    {
      continue;
    }

    // The samples between the two spikes must be < than the average of the
    // high spikes level. We don't test bits too near to the high levels as
    // signals can be out of phase so part of the energy can be in the near
    // samples.
    high = (mag[j]+mag[j+2]+mag[j+7]+mag[j+9])/6;
    if (mag[j+4] >= high ||
        mag[j+5] >= high)
    {
      continue;
    }

    // Similarly samples in the range 11-14 must be low, as it is the space
    // between the preamble and real data. Again we don't test bits too
    // near to high levels, see above.
    if (mag[j+11] >= high ||
        mag[j+12] >= high ||
        mag[j+13] >= high ||
        mag[j+14] >= high)
    {
      continue;
    }

good_preamble:
    // If the previous attempt with this message failed, retry using
    // magnitude correction.
    if (use_correction) {
      memcpy(aux, mag+j+MODE_S_PREAMBLE_US*2, sizeof(aux));
      if (j && detect_out_of_phase(mag+j)) {
        apply_phase_correction(mag+j);
      }
      // TODO ... apply other kind of corrections.
    }

    // Decode all the next 112 bits, regardless of the actual message size.
    // We'll check the actual message type later.
    errors = 0;
    for (i = 0; i < MODE_S_LONG_MSG_BITS*2; i += 2) {
      low = mag[j+i+MODE_S_PREAMBLE_US*2];
      high = mag[j+i+MODE_S_PREAMBLE_US*2+1];
      delta = low-high;
      if (delta < 0) delta = -delta;

      if (i > 0 && delta < 256) {
        bits[i/2] = bits[i/2-1];
      } else if (low == high) {
        // Checking if two adiacent samples have the same magnitude is
        // an effective way to detect if it's just random noise that
        // was detected as a valid preamble.
        bits[i/2] = 2; // error
        if (i < MODE_S_SHORT_MSG_BITS*2) errors++;
      } else if (low > high) {
        bits[i/2] = 1;
      } else {
        // (low < high) for exclusion 
        bits[i/2] = 0;
      }
    }

    // Restore the original message if we used magnitude correction.
    if (use_correction)
      memcpy(mag+j+MODE_S_PREAMBLE_US*2, aux, sizeof(aux));

    // Pack bits into bytes
    for (i = 0; i < MODE_S_LONG_MSG_BITS; i += 8) {
      msg[i/8] =
          bits[i]<<7 | 
          bits[i+1]<<6 | 
          bits[i+2]<<5 | 
          bits[i+3]<<4 | 
          bits[i+4]<<3 | 
          bits[i+5]<<2 | 
          bits[i+6]<<1 | 
          bits[i+7];
    }

    int msgtype = msg[0]>>3;
    int msglen = mode_s_msg_len_by_type(msgtype)/8;

    // Last check, high and low bits are different enough in magnitude to
    // mark this as real message and not just noise?
    delta = 0;
    for (i = 0; i < msglen*8*2; i += 2) {
      delta += abs(mag[j+i+MODE_S_PREAMBLE_US*2]-
                   mag[j+i+MODE_S_PREAMBLE_US*2+1]);
    }
    delta /= msglen*4;

    // Filter for an average delta of three is small enough to let almost
    // every kind of message to pass, but high enough to filter some random
    // noise.
    if (delta < 10*255) {
      use_correction = 0;
      continue;
    }

    // If we reached this point, and error is zero, we are very likely with
    // a Mode S message in our hands, but it may still be broken and CRC
    // may not be correct. This is handled by the next layer.
    if (errors == 0 || (self->aggressive && errors < 3)) {
      struct mode_s_msg mm;

      // Decode the received message
      mode_s_decode(self, &mm, msg);

      // Skip this message if we are sure it's fine.
      if (mm.crcok) {
        j += (MODE_S_PREAMBLE_US+(msglen*8))*2;
        good_message = 1;
        if (use_correction)
          mm.phase_corrected = 1;
      }

      // Pass data to the next layer
      if (self->check_crc == 0 || mm.crcok) {
        cb(self, &mm);
      }
    }

    // Retry with phase correction if possible.
    if (!good_message && !use_correction) {
      j--;
      use_correction = 1;
    } else {
      use_correction = 0;
    }
  }
}