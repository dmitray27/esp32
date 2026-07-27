#define _GNU_SOURCE
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include "afsk_protocol.h"
#include "afsk_decoder.h"
volatile uint32_t tx_bit_r_idx = 0;
int64_t g_virtual_us = 0;
#define FS 48000

/* Idle-line test: white + low-frequency-heavy noise, optionally a weak stray
 * tone, to see whether the receiver invents a preamble when nothing is sent.
 * usage: p <noise amp> <seconds> [stray tone Hz] [stray tone amp] */
int main(int argc, char **argv) {
    double amp = (argc > 1) ? atof(argv[1]) : 0.02;
    int seconds = (argc > 2) ? atoi(argv[2]) : 30;
    double tone_f = (argc > 3) ? atof(argv[3]) : 0.0;
    double tone_a = (argc > 4) ? atof(argv[4]) : 0.0;
    unsigned seed = 12345;

    afsk_decoder_t dec; afsk_message_t msg; memset(&msg, 0, sizeof(msg));
    g_virtual_us = 0; afsk_decoder_init(&dec, FS);

    int max_run = 0, locked = 0, frames = 0;
    double lp = 0.0, hum_ph = 0.0, tone_ph = 0.0;
    for (int i = 0; i < FS * seconds; i++) {
        g_virtual_us += 1000000 / FS;
        double w = ((double)rand_r(&seed) / RAND_MAX) * 2.0 - 1.0;
        lp += 0.02 * (w - lp);
        hum_ph += 2.0 * M_PI * 50.0 / FS;
        tone_ph += 2.0 * M_PI * tone_f / FS;
        double n = amp * (0.5 * w + 3.0 * lp + 0.3 * sin(hum_ph))
                 + tone_a * sin(tone_ph);
        if (afsk_decoder_process_sample(&dec, (int32_t)(n * 8388608.0), &msg)) frames++;
        if (dec.ones_count > max_run) max_run = dec.ones_count;
        if (dec.state != WAITING_FOR_PREAMBLE) locked++;
    }
    printf("baud=%d amp=%.3f tone=%.0fHz/%.4f  max_preamble_run=%d/%d  locked_samples=%d  frames=%d\n",
           BAUD_RATE, amp, tone_f, tone_a, max_run, PREAMBLE_BITS * 3 / 4, locked, frames);
    return 0;
}
