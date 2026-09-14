// Handle-unit serial protocol implementation (ESP32 side).
// See handle_protocol.h for the frame format.

#include <stdbool.h>
#include "handle_protocol.h"

uint16_t handle_crc16(const uint8_t *data, size_t len)
{
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= (uint16_t)data[i] << 8;
    for (int b = 0; b < 8; ++b) {
      if (crc & 0x8000) {
        crc = (uint16_t)((crc << 1) ^ 0x1021);
      } else {
        crc = (uint16_t)(crc << 1);
      }
    }
  }
  return crc;
}

static size_t stuff_payload(const uint8_t *payload, size_t len, uint8_t *out)
{
  size_t o = 0;
  for (size_t i = 0; i < len; ++i) {
    uint8_t b = payload[i];
    if (b == SOF1 || b == SOF2 || b == ESC) {
      out[o++] = ESC;
      out[o++] = (uint8_t)(b ^ 0x20);
    } else {
      out[o++] = b;
    }
  }
  return o;
}

size_t handle_encode(uint8_t msg_type, const uint8_t *payload, size_t len,
                     uint8_t seq, uint8_t *out)
{
  uint8_t stuffed[MAX_PAYLOAD * 2 + 8];
  size_t slen = stuff_payload(payload, len, stuffed);

  out[0] = SOF1;
  out[1] = SOF2;
  out[2] = msg_type;
  out[3] = (uint8_t)slen;
  out[4] = seq;
  for (size_t i = 0; i < slen; ++i) {
    out[5 + i] = stuffed[i];
  }
  // CRC over TYPE..PAYLOAD (bytes 2 .. 5+slen).
  uint16_t crc = handle_crc16(&out[2], 3 + slen);
  out[5 + slen] = (uint8_t)(crc & 0xFF);
  out[6 + slen] = (uint8_t)(crc >> 8);
  return 7 + slen;
}

static size_t unstuff_payload(const uint8_t *data, size_t len, uint8_t *out)
{
  size_t o = 0;
  for (size_t i = 0; i < len; ++i) {
    if (data[i] == ESC && i + 1 < len) {
      out[o++] = (uint8_t)(data[i + 1] ^ 0x20);
      ++i;
    } else {
      out[o++] = data[i];
    }
  }
  return o;
}

int handle_decoder_feed(HandleDecoder *dec, const uint8_t *data, size_t n,
                        void (*on_frame)(uint8_t type, const uint8_t *payload,
                                         size_t len, uint8_t seq))
{
  // Append to buffer (drop if it would overflow).
  if (dec->len + n > sizeof(dec->buf)) {
    dec->len = 0;
  }
  for (size_t i = 0; i < n; ++i) {
    dec->buf[dec->len++] = data[i];
  }

  int frames = 0;
  while (true) {
    // Find start marker.
    size_t start = 0;
    while (start + 1 < dec->len &&
           !(dec->buf[start] == SOF1 && dec->buf[start + 1] == SOF2)) {
      ++start;
    }
    if (start > 0) {
      // Shift remaining bytes to the front.
      for (size_t i = start; i < dec->len; ++i) {
        dec->buf[i - start] = dec->buf[i];
      }
      dec->len -= start;
    }
    if (dec->len < 7) {
      break;  // need at least header + CRC
    }
    uint8_t msg_type = dec->buf[2];
    size_t slen = dec->buf[3];
    uint8_t seq = dec->buf[4];
    size_t total = 5 + slen + 2;
    if (dec->len < total) {
      break;  // incomplete
    }
    uint16_t crc_recv = (uint16_t)(dec->buf[5 + slen]) |
                        ((uint16_t)dec->buf[6 + slen] << 8);
    uint16_t crc_calc = handle_crc16(&dec->buf[2], 3 + slen);
    // Remove the frame from the buffer.
    for (size_t i = total; i < dec->len; ++i) {
      dec->buf[i - total] = dec->buf[i];
    }
    dec->len -= total;
    if (crc_recv != crc_calc) {
      continue;  // corrupt; drop
    }
    uint8_t payload[MAX_PAYLOAD];
    size_t plen = unstuff_payload(&dec->buf[0], slen, payload);
    if (on_frame) {
      on_frame(msg_type, payload, plen, seq);
    }
    ++frames;
  }
  return frames;
}