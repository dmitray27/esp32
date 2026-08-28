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
static bool bit_buf[16384];
static volatile uint32_t w_idx;

static int one_case(const char *payload, double drift) {
    double fs_actual = FS * (1.0 + drift / 100.0);
    tx_bit_r_idx = 0; w_idx = 0; memset(bit_buf, 0, sizeof(bit_buf));
    afsk_serialize_block((const uint8_t*)payload, strlen(payload), bit_buf, &w_idx, 16384);
    uint32_t nbits = w_idx;
    afsk_decoder_t dec; afsk_message_t msg; memset(&msg,0,sizeof(msg));
    g_virtual_us = 0; afsk_decoder_init(&dec, FS);
    int spb = (int)(fs_actual/BAUD_RATE + 0.5);
    double phase = 0; bool got=false; char d[512]={0}; bool crc=false;
    for (uint32_t b=0;b<nbits && !got;b++){
        double f = bit_buf[b]?MARK_FREQ:SPACE_FREQ;
        for(int i=0;i<spb;i++){ phase += 2.0*M_PI*f/fs_actual;
            int32_t smp=(int32_t)(0.30*sin(phase)*8388608.0); g_virtual_us += 1000000/FS;
            if (afsk_decoder_process_sample(&dec,smp,&msg)){got=true;memcpy(d,msg.text,msg.length);d[msg.length]=0;crc=msg.crc_valid;break;} } }
    for(int i=0;i<FS*3 && !got;i++){ g_virtual_us+=1000000/FS; if(afsk_decoder_process_sample(&dec,0,&msg)){got=true;memcpy(d,msg.text,msg.length);d[msg.length]=0;crc=msg.crc_valid;}}
    return got && crc && strcmp(d,payload)==0;
}

int main(void){
    const char *payloads[] = {"AFSK over ESP32","quadrature+DPLL","The quick brown fox 0123","Hi","0xFF test \xff\xff\xff"};
    double lo = -2.0, hi = -2.0;
    for (double drift=-3.0; drift<=3.01; drift+=0.1) {
        int all = 1;
        for (int p=0;p<5;p++) if(!one_case(payloads[p], drift)) { all=0; break; }
        if (all) { if (lo < -1.9) lo = drift; hi = drift; }
    }
    printf("pass window (all payloads): [%+.1f%% .. %+.1f%%], center=%+.2f%%, width=%.1f%%\n",
           lo, hi, (lo+hi)/2.0, hi-lo);
    return 0;
}
