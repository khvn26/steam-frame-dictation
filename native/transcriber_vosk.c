#include "app.h"
#include "whisper.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *extract_text(const char *json) {
    const char *k = strstr(json, "\"text\""); if(!k) return strdup("");
    const char *c = strchr(k, ':'); if(!c) return strdup("");
    const char *q = strchr(c, '"'); if(!q) return strdup("");
    q++;
    struct bytes out={0};
    for (const char *p=q; *p; p++) {
        if (*p=='"') break;
        if (*p=='\\' && p[1]) {
            p++;
            char ch=*p;
            if(ch=='n') ch='\n'; else if(ch=='t') ch='\t'; else if(ch=='r') ch='\r';
            bytes_append(&out,&ch,1);
        } else bytes_append(&out,p,1);
    }
    char nul=0; bytes_append(&out,&nul,1);
    return (char*)out.p;
}

static char *trim_dup(const char *s) {
    while (*s == ' ' || *s == '\n' || *s == '\t' || *s == '\r') s++;
    size_t n = strlen(s);
    while (n && (s[n-1] == ' ' || s[n-1] == '\n' || s[n-1] == '\t' || s[n-1] == '\r')) n--;
    char *out = malloc(n + 1);
    if (!out) return strdup("");
    memcpy(out, s, n); out[n] = 0;
    return out;
}

static bool is_bad_text(const char *text) {
    if (!text) return true;
    int alnum = 0;
    int punct = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        if (isalnum(*p)) alnum++;
        else if (!isspace(*p)) punct++;
    }
    if (alnum == 0) return true;
    if (alnum <= 1 && punct > 0) return true;
    if (!strcmp(text, "[") || !strcmp(text, "]") || !strcmp(text, "(") || !strcmp(text, ")")) return true;
    return false;
}

static char *transcribe_vosk(app *a, const uint8_t *pcm, size_t n) {
    VoskRecognizer *r = vosk_recognizer_new(a->model, (float)a->rate);
    vosk_recognizer_set_words(r, 0);
    struct bytes text={0};
    for(size_t off=0; off<n; off += (size_t)a->chunk_bytes) {
        int len = (int)((n-off < (size_t)a->chunk_bytes) ? n-off : (size_t)a->chunk_bytes);
        if (vosk_recognizer_accept_waveform(r, (const char*)pcm+off, len)) {
            char *t = extract_text(vosk_recognizer_result(r));
            if(t && *t) { if(text.n) bytes_append(&text," ",1); bytes_append(&text,t,strlen(t)); }
            free(t);
        }
    }
    char *ft = extract_text(vosk_recognizer_final_result(r));
    if(ft && *ft) { if(text.n) bytes_append(&text," ",1); bytes_append(&text,ft,strlen(ft)); }
    free(ft);
    vosk_recognizer_free(r);
    char nul=0; bytes_append(&text,&nul,1);
    return (char*)text.p;
}

static char *transcribe_whisper_lib(app *a, const uint8_t *pcm, size_t n) {
    if (!a->whisper_ctx) return strdup("");
    size_t samples = n / 2;
    float *f32 = malloc(samples * sizeof(float));
    if (!f32) return strdup("");
    const int16_t *s16 = (const int16_t *)pcm;
    for (size_t i = 0; i < samples; i++) f32[i] = (float)s16[i] / 32768.0f;

    struct whisper_full_params p = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    p.n_threads = a->whisper_threads;
    p.language = "en";
    p.translate = false;
    p.no_context = true;
    p.no_timestamps = true;
    p.single_segment = true;
    p.audio_ctx = a->whisper_audio_ctx;
    p.max_tokens = a->whisper_max_tokens;
    p.print_special = false;
    p.print_progress = false;
    p.print_realtime = false;
    p.print_timestamps = false;
    p.suppress_blank = true;
    p.temperature = 0.0f;
    p.temperature_inc = 0.0f;

    int rc = whisper_full(a->whisper_ctx, p, f32, (int)samples);
    free(f32);
    if (rc != 0) return strdup("");

    struct bytes out = {0};
    int nseg = whisper_full_n_segments(a->whisper_ctx);
    for (int i = 0; i < nseg; i++) {
        const char *t = whisper_full_get_segment_text(a->whisper_ctx, i);
        if (t && *t) {
            if (out.n) bytes_append(&out, " ", 1);
            bytes_append(&out, t, strlen(t));
        }
    }
    char nul = 0; bytes_append(&out, &nul, 1);
    char *trimmed = trim_dup((char *)out.p);
    bytes_free(&out);
    return trimmed;
}

static char *transcribe_segment(app *a, const uint8_t *pcm, size_t n) {
    if (!strcmp(a->provider, "whisper")) return transcribe_whisper_lib(a, pcm, n);
    return transcribe_vosk(a, pcm, n);
}

void *segment_thread_main(void *vp) {
    app *a = vp; struct bytes pcm={0}; bool append_return=false;
    while(segq_pop(&a->segq, &pcm, &append_return)) {
        if (pcm.n) {
            double duration = (double)pcm.n / 2.0 / (double)a->rate;
            double rms = pcm_rms(pcm.p, pcm.n);
            if (duration < a->min_segment_seconds || rms < a->min_transcribe_rms) {
                fprintf(stderr,"drop segment: %.2fs rms %.0f\n", duration, rms);
                bytes_free(&pcm);
            } else {
                double t0=monotonic_s();
                char *text = transcribe_segment(a, pcm.p, pcm.n);
                if(text && *text && !is_bad_text(text)) {
                    fprintf(stderr,"heard: '%s' (%.2fs)\n", text, monotonic_s()-t0);
                    size_t len=strlen(text); char *with_space=malloc(len+2);
                    if(with_space) { memcpy(with_space,text,len); with_space[len]=' '; with_space[len+1]=0; textq_push(&a->textq, with_space); }
                } else if (text && *text) {
                    fprintf(stderr,"drop transcript: '%s'\n", text);
                } else fprintf(stderr,"no transcript\n");
                free(text); bytes_free(&pcm);
            }
        }
        if (append_return) textq_push(&a->textq, strdup("\n"));
    }
    textq_done(&a->textq);
    return NULL;
}
