#include "app.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

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

static int write_wav_file(const char *path, const uint8_t *pcm, size_t n, int rate) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    uint32_t data_size = (uint32_t)n;
    uint32_t riff_size = 36 + data_size;
    uint16_t audio_format = 1, channels = 1, bits = 16, block_align = 2;
    uint32_t byte_rate = (uint32_t)rate * block_align;
    fwrite("RIFF",1,4,f); fwrite(&riff_size,4,1,f); fwrite("WAVE",1,4,f);
    fwrite("fmt ",1,4,f); uint32_t fmt_size=16; fwrite(&fmt_size,4,1,f);
    fwrite(&audio_format,2,1,f); fwrite(&channels,2,1,f); fwrite(&rate,4,1,f);
    fwrite(&byte_rate,4,1,f); fwrite(&block_align,2,1,f); fwrite(&bits,2,1,f);
    fwrite("data",1,4,f); fwrite(&data_size,4,1,f); fwrite(pcm,1,n,f);
    int ok = ferror(f) ? -1 : 0;
    fclose(f);
    return ok;
}

static char *transcribe_whisper(app *a, const uint8_t *pcm, size_t n) {
    char tmpl[] = "/tmp/frame-dict-whisper-XXXXXX.wav";
    int fd = mkstemps(tmpl, 4);
    if (fd < 0) return strdup("");
    close(fd);
    if (write_wav_file(tmpl, pcm, n, a->rate) != 0) { unlink(tmpl); return strdup(""); }

    int pipefd[2];
    if (pipe(pipefd) != 0) { unlink(tmpl); return strdup(""); }
    pid_t pid = fork();
    if (pid == 0) {
        dup2(pipefd[1], STDOUT_FILENO);
        int nullfd = open("/dev/null", O_WRONLY);
        if (nullfd >= 0) { dup2(nullfd, STDERR_FILENO); close(nullfd); }
        close(pipefd[0]); close(pipefd[1]);
        char threads[32]; snprintf(threads, sizeof(threads), "%d", a->whisper_threads);
        execl(a->whisper_cli, a->whisper_cli,
              "-m", a->whisper_model,
              "-f", tmpl,
              "-nt", "-np", "-t", threads,
              (char *)NULL);
        _exit(127);
    }
    close(pipefd[1]);
    struct bytes out={0};
    char buf[4096];
    ssize_t nr;
    while ((nr = read(pipefd[0], buf, sizeof(buf))) > 0) bytes_append(&out, buf, (size_t)nr);
    close(pipefd[0]);
    int st; waitpid(pid, &st, 0);
    unlink(tmpl);
    char nul=0; bytes_append(&out,&nul,1);
    char *trimmed = trim_dup((char*)out.p);
    bytes_free(&out);
    return trimmed;
}

static char *transcribe_segment(app *a, const uint8_t *pcm, size_t n) {
    if (!strcmp(a->provider, "whisper")) return transcribe_whisper(a, pcm, n);
    return transcribe_vosk(a, pcm, n);
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
