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

/* Squelch calibration test: a noisy idle line (radio hiss with the volume up)
 * followed by a real transmission. The receiver must stay quiet on the hiss,
 * raise its squelch above it, and still decode the message.
 * usage: squelch [hiss amp] [signal amp] [idle seconds] */

static unsigned g_seed = 4242;

static double hiss(double amp) {
    double w = ((double)rand_r(&g_seed) / RAND_MAX) * 2.0 - 1.0;
    return amp * w;
}

static void feed(afsk_decoder_t *dec, afsk_message_t *msg, double sample,
                 int *frames, int *max_run) {
    g_virtual_us += 1000000 / FS;
    if (afsk_decoder_process_sample(dec, (int32_t)(sample * 8388608.0), msg)) {
        (*frames)++;
    }
    if (dec->ones_count > *max_run) *max_run = dec->ones_count;
}

int main(int argc, char **argv) {
    double hiss_amp = (argc > 1) ? atof(argv[1]) : 0.05;   /* -26 dBFS */
    double sig_amp  = (argc > 2) ? atof(argv[2]) : 0.30;   /* -10 dBFS */
    int idle_s      = (argc > 3) ? atoi(argv[3]) : 20;

    const char *text = "SQUELCH TEST";
    size_t len = strlen(text);

    static bool bits[64 * 1024];
    uint32_t nbits = 0;
    afsk_serialize_block((const uint8_t *)text, len, bits, &nbits, sizeof(bits));

    afsk_decoder_t dec;
    afsk_message_t msg;
    memset(&msg, 0, sizeof(msg));
    g_virtual_us = 0;
    afsk_decoder_init(&dec, FS);

    int frames = 0, idle_run = 0;

    /* Phase 1: idle hiss only. */
    for (int i = 0; i < FS * idle_s; i++) {
        feed(&dec, &msg, hiss(hiss_amp), &frames, &idle_run);
    }
    int idle_frames = frames;
    float squelch_after_idle = dec.squelch_amp;

    /* Phase 2: the same hiss with an AFSK burst on top. */
    int sig_run = 0;
    double phase = 0.0;
    int spb = FS / BAUD_RATE;
    for (uint32_t b = 0; b < nbits; b++) {
        double f = bits[b] ? MARK_FREQ : SPACE_FREQ;
        for (int i = 0; i < spb; i++) {
            phase += 2.0 * M_PI * f / FS;
            feed(&dec, &msg, sig_amp * sin(phase) + hiss(hiss_amp), &frames, &sig_run);
        }
    }
    for (int i = 0; i < FS; i++) {
        feed(&dec, &msg, hiss(hiss_amp), &frames, &sig_run);
    }

    bool ok = (idle_frames == 0) && (idle_run < PREAMBLE_BITS / 4) &&
              (frames == 1) && msg.crc_valid &&
              msg.length == len && memcmp(msg.text, text, len) == 0;

    printf("baud=%d hiss=%.1f dBFS signal=%.1f dBFS | squelch after idle: %.1f dBFS\n",
           BAUD_RATE, 20.0 * log10(hiss_amp), 20.0 * log10(sig_amp),
           20.0 * log10f(squelch_after_idle));
    printf("idle: frames=%d max_preamble_run=%d/%d | signal: frames=%d crc=%s text='%.*s'\n",
           idle_frames, idle_run, PREAMBLE_BITS * 3 / 4,
           frames - idle_frames, msg.crc_valid ? "OK" : "BAD",
           (int)msg.length, msg.text);
    printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
