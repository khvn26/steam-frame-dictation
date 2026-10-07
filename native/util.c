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
