#pragma once
#include <stdint.h>
#include <stddef.h>

/* Shared AFSK link parameters (identical for TX and RX). */
#define MARK_FREQ       1200        /* Hz, logical '1' */
#define SPACE_FREQ      2200        /* Hz, logical '0' */
#define BAUD_RATE       300         /* bits per second */
#define PREAMBLE_BITS   960         /* leading '1's before a frame */
#define SAMPLE_RATE     48000       /* Hz, I2S RX sample rate */

/* Diagnostic chatter: preamble progress, idle level meter and the periodic
 * "waiting for signal" line. Set to 0 for a quiet log with received blocks
 * and assembled messages only. */
#ifndef AFSK_VERBOSE
#define AFSK_VERBOSE    1
#endif

/* Squelch calibration: measure the idle line and keep the absolute threshold
 * just above it, so the receiver adapts to the audio level of a particular
 * radio. Set to 0 to use the fixed MIN_TONE_AMPLITUDE (wired link). */
#ifndef AFSK_AUTO_SQUELCH
#define AFSK_AUTO_SQUELCH   1
#endif

/* CRC-8 (poly 0x07, init 0x00). Single shared implementation. */
uint8_t afsk_crc8(const uint8_t *data, size_t len);
