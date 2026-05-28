#include "b64.h"
#include <stdlib.h>
#include <stdint.h>

/* -2 = padding '=',  -1 = ignore (whitespace or invalid byte) */
static const int8_t DTAB[256] = {
/* 00 */ -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
/* 10 */ -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
/* 20 */ -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
/* 30 */ 52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-2,-1,-1,
/* 40 */ -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
/* 50 */ 15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
/* 60 */ -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
/* 70 */ 41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
/* 80 */ -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
/* 90 */ -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
/* A0 */ -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
/* B0 */ -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
/* C0 */ -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
/* D0 */ -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
/* E0 */ -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
/* F0 */ -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
};

unsigned char *b64_decode(const char *src, size_t src_len, size_t *out_len) {
    *out_len = 0;
    // Upper bound: 3 output bytes per 4 input chars, plus a small margin.
    size_t max_out = (src_len / 4) * 3 + 4;
    unsigned char *out = malloc(max_out);
    if (!out) return NULL;

    uint32_t acc  = 0;
    int      bits = 0;
    size_t   oi   = 0;

    for (size_t i = 0; i < src_len; i++) {
        int8_t v = DTAB[(unsigned char)src[i]];
        if (v == -2) break;    // padding '=': stop
        if (v <   0) continue; // whitespace / invalid: skip

        acc  = (acc << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out[oi++] = (unsigned char)((acc >> bits) & 0xFF);
        }
    }

    *out_len = oi;
    return out;
}
