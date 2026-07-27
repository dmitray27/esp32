#include "afsk_protocol.h"
#include "esp_log.h"
#include <string.h>
#include <inttypes.h>

static const char *TAG = "AFSK_PROTO";

extern volatile uint32_t tx_bit_r_idx;

static void push_tx_bit(volatile bool *bit_buffer,
                        volatile uint32_t *w_idx,
                        uint32_t buffer_size,
                        bool bit) {
    uint32_t next_w_idx = (*w_idx + 1) % buffer_size;
    if (next_w_idx != tx_bit_r_idx) {
        bit_buffer[*w_idx] = bit;
        *w_idx = next_w_idx;
    }
}

void afsk_serialize_block(const uint8_t *data, size_t len,
                          volatile bool *bit_buffer,
                          volatile uint32_t *w_idx,
                          uint32_t buffer_size) {
    (void)TAG;

    for (int i = 0; i < PREAMBLE_BITS; i++) {
        push_tx_bit(bit_buffer, w_idx, buffer_size, 1);
    }

    for (size_t i = 0; i < len; i++) {
        uint8_t byte = data[i];
        push_tx_bit(bit_buffer, w_idx, buffer_size, 0);  // START
        for (int b = 0; b < 8; b++) {
            push_tx_bit(bit_buffer, w_idx, buffer_size, (byte >> b) & 0x01);
        }
        push_tx_bit(bit_buffer, w_idx, buffer_size, 1);  // STOP
    }

    uint8_t crc = afsk_crc8(data, len);
    push_tx_bit(bit_buffer, w_idx, buffer_size, 0);
    for (int b = 0; b < 8; b++) {
        push_tx_bit(bit_buffer, w_idx, buffer_size, (crc >> b) & 0x01);
    }
    push_tx_bit(bit_buffer, w_idx, buffer_size, 1);

    for (int i = 0; i < 10; i++) {
        push_tx_bit(bit_buffer, w_idx, buffer_size, 1);
    }
}

/* Largest byte count <= max_len starting at `start` that does not split a
 * multibyte UTF-8 character. A continuation byte matches 10xxxxxx (0x80..0xBF);
 * if the byte just past the block is a continuation byte we are mid-character,
 * so back off until the next block would start on a character boundary. */
int afsk_utf8_block_len(const char *buf, int start, int len, int max_len) {
    int n = (len - start < max_len) ? (len - start) : max_len;
    if (start + n < len) {
        while (n > 0 && ((unsigned char)buf[start + n] & 0xC0) == 0x80) {
            n--;
        }
    }
    if (n <= 0) {
        n = (len - start < max_len) ? (len - start) : max_len;
    }
    return n;
}

uint8_t afsk_crc8(const uint8_t *data, size_t len) {
    uint8_t crc = 0x00;
    while (len--) {
        crc ^= *data++;
        for (uint8_t i = 0; i < 8; i++) {
            crc = (crc & 0x80) ? ((crc << 1) ^ 0x07) : (crc << 1);
        }
    }
    return crc;
}
