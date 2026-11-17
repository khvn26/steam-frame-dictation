#include "app.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void bytes_free(struct bytes *b) { free(b->p); b->p = NULL; b->n = b->cap = 0; }
int bytes_append(struct bytes *b, const void *p, size_t n) {
    if (!n) return 0;
    if (b->n + n > b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 8192;
        while (nc < b->n + n) nc *= 2;
        uint8_t *np = realloc(b->p, nc);
        if (!np) return -1;
        b->p = np; b->cap = nc;
    }
    memcpy(b->p + b->n, p, n); b->n += n; return 0;
}
void bytes_clear(struct bytes *b) { b->n = 0; }

double monotonic_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}
void msleep(int ms) {
    struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}
double pcm_rms(const uint8_t *data, size_t n) {
    size_t samples = n / 2; if(!samples) return 0.0;
    const int16_t *s = (const int16_t*)data;
    double sum = 0.0;
    for(size_t i=0;i<samples;i++) sum += (double)s[i]*(double)s[i];
    return sqrt(sum / (double)samples);
}

// Decode the last UTF-8 codepoint of s; 0 for an empty or malformed tail.
uint32_t utf8_last_codepoint(const char *s) {
    size_t n = s ? strlen(s) : 0;
    if (!n) return 0;
    const unsigned char *u = (const unsigned char *)s;
    size_t i = n - 1;
    while (i > 0 && (u[i] & 0xC0) == 0x80 && n - i < 4) i--;
    size_t len = n - i;
    if (u[i] < 0x80) return len == 1 ? u[i] : 0;
    if ((u[i] & 0xE0) == 0xC0 && len == 2) return ((uint32_t)(u[i] & 0x1F) << 6) | (u[i+1] & 0x3F);
    if ((u[i] & 0xF0) == 0xE0 && len == 3) return ((uint32_t)(u[i] & 0x0F) << 12) | ((uint32_t)(u[i+1] & 0x3F) << 6) | (u[i+2] & 0x3F);
    if ((u[i] & 0xF8) == 0xF0 && len == 4) return ((uint32_t)(u[i] & 0x07) << 18) | ((uint32_t)(u[i+1] & 0x3F) << 12) | ((uint32_t)(u[i+2] & 0x3F) << 6) | (u[i+3] & 0x3F);
    return 0;
}
// Scripts written without spaces between words (Chinese, Japanese) and their punctuation.
bool codepoint_is_cjk(uint32_t cp) {
    return (cp >= 0x2E80 && cp <= 0x9FFF)    // radicals, CJK punctuation, kana, ideographs
        || (cp >= 0xF900 && cp <= 0xFAFF)    // compatibility ideographs
        || (cp >= 0xFF00 && cp <= 0xFFEF)    // fullwidth forms
        || (cp >= 0x20000 && cp <= 0x3FFFF); // ideograph extensions
}
