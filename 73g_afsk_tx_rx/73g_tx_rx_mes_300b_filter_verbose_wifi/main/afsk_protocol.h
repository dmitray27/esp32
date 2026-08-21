#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "afsk_common.h"

void afsk_serialize_block(const uint8_t *data, size_t len,
                          volatile bool *bit_buffer,
                          volatile uint32_t *w_idx,
                          uint32_t buffer_size);

int afsk_utf8_block_len(const char *buf, int start, int len, int max_len);
