#define _GNU_SOURCE
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
/* Host-side end-to-end test of the AFSK link DSP:
 *   TX serialize -> synthesize MARK/SPACE tones -> quadrature+DPLL decoder.
 * Verifies payload and CRC under nominal, clock-drift and noisy conditions. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "afsk_protocol.h"
#include "afsk_decoder.h"

/* Symbols normally provided by tx_ad9851.c */
volatile uint32_t tx_bit_r_idx = 0;

/* Virtual clock backing esp_timer stub */
int64_t g_virtual_us = 0;

#define FS            48000
#define FULL_SCALE    8388608.0   /* 2^23 */

static bool  bit_buf[16384];
static volatile uint32_t w_idx;

static int total_tests = 0, passed_tests = 0;

static bool run_case(const char *name, const char *payload,
                     double fs_actual, double noise_amp) {
    total_tests++;

    /* --- serialize one block --- */
    tx_bit_r_idx = 0;
    w_idx = 0;
    memset(bit_buf, 0, sizeof(bit_buf));
    afsk_serialize_block((const uint8_t *)payload, strlen(payload),
                         bit_buf, &w_idx, sizeof(bit_buf) / sizeof(bit_buf[0]));
    uint32_t nbits = w_idx;

    /* --- decoder --- */
    afsk_decoder_t dec;
    afsk_message_t msg;
    memset(&msg, 0, sizeof(msg));
    g_virtual_us = 0;
    afsk_decoder_init(&dec, FS);

    int samples_per_bit_gen = (int)(fs_actual / BAUD_RATE + 0.5);
    double phase = 0.0;
    bool got = false;
    char decoded[512] = {0};
    bool crc_ok = false;

    unsigned int seed = 12345;
    /* small lead-in of silence */
    for (int i = 0; i < FS / 10; i++) {
        g_virtual_us += 1000000 / FS;
        double n = noise_amp * (((double)rand_r(&seed) / RAND_MAX) * 2.0 - 1.0);
        int32_t smp = (int32_t)(n * FULL_SCALE);
        if (afsk_decoder_process_sample(&dec, smp, &msg)) { got = true; }
    }

    for (uint32_t b = 0; b < nbits && !got; b++) {
        double f = bit_buf[b] ? (double)MARK_FREQ : (double)SPACE_FREQ;
        for (int i = 0; i < samples_per_bit_gen; i++) {
            phase += 2.0 * M_PI * f / fs_actual;
            double v = 0.30 * sin(phase);
            double n = noise_amp * (((double)rand_r(&seed) / RAND_MAX) * 2.0 - 1.0);
            int32_t smp = (int32_t)((v + n) * FULL_SCALE);
            g_virtual_us += 1000000 / FS;
            if (afsk_decoder_process_sample(&dec, smp, &msg)) {
                got = true;
                memcpy(decoded, msg.text, msg.length);
                decoded[msg.length] = '\0';
                crc_ok = msg.crc_valid;
                break;
            }
        }
    }

    /* flush with trailing silence so the frame finalizes if needed
     * (> SIGNAL_TIMEOUT_MS so the timeout-finalize path can fire) */
    for (int i = 0; i < FS * 3 && !got; i++) {
        g_virtual_us += 1000000 / FS;
        if (afsk_decoder_process_sample(&dec, 0, &msg)) {
            got = true;
            memcpy(decoded, msg.text, msg.length);
            decoded[msg.length] = '\0';
            crc_ok = msg.crc_valid;
        }
    }

    bool ok = got && crc_ok && strcmp(decoded, payload) == 0;
    if (ok) passed_tests++;
    printf("[%s] %-22s got=%d crc=%d decoded=\"%s\" (expected \"%s\")\n",
           ok ? "PASS" : "FAIL", name, got, crc_ok, got ? decoded : "", payload);
    return ok;
}

/* Splits a message into UTF-8-safe blocks like tx_task does, sends each one
 * through tones + decoder, and reassembles the payloads the way rx_task does.
 * Checks that every block is valid UTF-8 on its own and that the concatenation
 * is byte-identical to the original message. */
static bool run_multiblock_case(const char *name, const char *message, int max_block) {
    total_tests++;

    int len = (int)strlen(message);
    char assembled[4096] = {0};
    int assembled_len = 0;
    int blocks = 0;
    bool all_ok = true;
    bool split_clean = true;

    for (int start = 0; start < len; ) {
        int block_len = afsk_utf8_block_len(message, start, len, max_block);
        char block[256];
        memcpy(block, message + start, block_len);
        block[block_len] = '\0';
        blocks++;

        /* a block must never begin with a UTF-8 continuation byte */
        if (((unsigned char)block[0] & 0xC0) == 0x80) split_clean = false;

        tx_bit_r_idx = 0;
        w_idx = 0;
        memset(bit_buf, 0, sizeof(bit_buf));
        afsk_serialize_block((const uint8_t *)block, block_len,
                             bit_buf, &w_idx, sizeof(bit_buf) / sizeof(bit_buf[0]));
        uint32_t nbits = w_idx;

        afsk_decoder_t dec;
        afsk_message_t msg;
        memset(&msg, 0, sizeof(msg));
        g_virtual_us = 0;
        afsk_decoder_init(&dec, FS);

        double phase = 0.0;
        bool got = false;
        for (int i = 0; i < FS / 10; i++) {
            g_virtual_us += 1000000 / FS;
            afsk_decoder_process_sample(&dec, 0, &msg);
        }
        for (uint32_t b = 0; b < nbits && !got; b++) {
            double f = bit_buf[b] ? (double)MARK_FREQ : (double)SPACE_FREQ;
            for (int i = 0; i < FS / BAUD_RATE; i++) {
                phase += 2.0 * M_PI * f / (double)FS;
                int32_t smp = (int32_t)(0.30 * sin(phase) * FULL_SCALE);
                g_virtual_us += 1000000 / FS;
                if (afsk_decoder_process_sample(&dec, smp, &msg)) { got = true; break; }
            }
        }

        if (!got || !msg.crc_valid || msg.length != block_len ||
            memcmp(msg.text, block, block_len) != 0) {
            all_ok = false;
            break;
        }
        memcpy(assembled + assembled_len, msg.text, msg.length);
        assembled_len += msg.length;
        start += block_len;
    }

    assembled[assembled_len] = '\0';
    bool ok = all_ok && split_clean && assembled_len == len &&
              memcmp(assembled, message, len) == 0;
    if (ok) passed_tests++;
    printf("[%s] %-22s blocks=%d assembled=%d/%d bytes, clean_split=%d\n",
           ok ? "PASS" : "FAIL", name, blocks, assembled_len, len, split_clean);
    return ok;
}

int main(void) {
    run_case("nominal",     "HELLO WORLD 123", (double)FS,        0.0);
    run_case("drift+0.3%",  "AFSK over ESP32", (double)FS * 1.003, 0.0);
    run_case("drift-0.3%",  "quadrature+DPLL", (double)FS * 0.997, 0.0);
    run_case("drift+0.6%",  "positive drift 6", (double)FS * 1.006, 0.0);
    run_case("drift-0.6%",  "negative drift 6", (double)FS * 0.994, 0.0);
    run_case("noise 5%",    "Noisy channel!!", (double)FS,        0.05);
    run_case("drift+noise", "Drift and noise", (double)FS * 1.002, 0.04);
    run_case("short",       "Hi", (double)FS, 0.0);
    run_case("long line",   "The quick brown fox jumps over 0123", (double)FS, 0.0);

    run_multiblock_case("assemble ascii",
        "Embedded systems combine hardware and software to work reliably.", 50);
    run_multiblock_case("assemble cyrillic",
        "Встраиваемые системы объединяют аппаратную и программную части.", 50);
    run_multiblock_case("assemble emoji",
        "😀😃😄😁😆😅🤣😂🙂🙃😇😉😊😋😌🥰😍🤩", 50);

    printf("\n=== %d/%d cases passed ===\n", passed_tests, total_tests);
    return (passed_tests == total_tests) ? 0 : 1;
}
