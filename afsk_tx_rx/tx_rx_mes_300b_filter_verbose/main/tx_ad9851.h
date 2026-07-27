#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "afsk_common.h"

/* AD9851 control lines */
#define PIN_FQ_UD   GPIO_NUM_18
#define PIN_W_CLK   GPIO_NUM_19
#define PIN_DATA    GPIO_NUM_21
#define PIN_RESET   GPIO_NUM_16

/* DDS reference clock fed to the AD9851 (external 125 MHz oscillator,
 * 6x refclk multiplier disabled -> control byte = 0). */
#define REF_CLOCK   125000000ULL

/* PTT keying for a voice radio (opto-isolator in place of the headset PTT
 * button). PTT_LEAD_MS lets the radio switch to transmit before the preamble
 * starts, PTT_TAIL_MS keeps it keyed so the last bit is not clipped.
 * Set TX_PTT_ENABLE to 0 for a direct wired link. */
#ifndef TX_PTT_ENABLE
#define TX_PTT_ENABLE       0
#endif
#define PIN_PTT             GPIO_NUM_17
#define PTT_ACTIVE_LEVEL    1       /* level that keys the radio */
#define PTT_LEAD_MS         300
#define PTT_TAIL_MS         100

#define TX_BIT_BUFFER_SIZE  4096

void tx_ad9851_init(void);
bool tx_ad9851_send_block(const uint8_t *data, size_t len);
bool tx_ad9851_is_active(void);
void tx_ad9851_wait_idle(void);

/* Manual keying, e.g. to hold the radio up across several blocks. */
void tx_ad9851_ptt(bool key);
