#include "app.h"
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

static char *transcribe_segment(app *a, const uint8_t *pcm, size_t n) {
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

void *segment_thread_main(void *vp) {
    app *a = vp; struct bytes pcm={0}; bool append_return=false;
    while(segq_pop(&a->segq, &pcm, &append_return)) {
        if (pcm.n) {
            double t0=monotonic_s();
            char *text = transcribe_segment(a, pcm.p, pcm.n);
            if(text && *text) {
                fprintf(stderr,"heard: '%s' (%.2fs)\n", text, monotonic_s()-t0);
                size_t len=strlen(text); char *with_space=malloc(len+2);
                if(with_space) { memcpy(with_space,text,len); with_space[len]=' '; with_space[len+1]=0; textq_push(&a->textq, with_space); }
            } else fprintf(stderr,"no transcript\n");
            free(text); bytes_free(&pcm);
        }
        if (append_return) textq_push(&a->textq, strdup("\n"));
    }
    textq_done(&a->textq);
    return NULL;
}
