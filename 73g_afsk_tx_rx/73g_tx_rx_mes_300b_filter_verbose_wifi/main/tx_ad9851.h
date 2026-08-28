#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "afsk_common.h"

/* AD9851 control lines. Pin numbers come from Kconfig ("AFSK Pin
 * Configuration"), so the same sources build for WROOM and for S3 by
 * swapping sdkconfig.defaults. */
#define PIN_FQ_UD   ((gpio_num_t)CONFIG_PIN_FQ_UD)
#define PIN_W_CLK   ((gpio_num_t)CONFIG_PIN_W_CLK)
#define PIN_DATA    ((gpio_num_t)CONFIG_PIN_DATA)
#define PIN_RESET   ((gpio_num_t)CONFIG_PIN_RESET)

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
#define PIN_PTT             ((gpio_num_t)CONFIG_PIN_PTT)
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
