#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <math.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

// Minimal Vosk C API declarations.
typedef struct VoskModel VoskModel;
typedef struct VoskRecognizer VoskRecognizer;
extern void vosk_set_log_level(int log_level);
extern VoskModel *vosk_model_new(const char *model_path);
extern void vosk_model_free(VoskModel *model);
extern VoskRecognizer *vosk_recognizer_new(VoskModel *model, float sample_frequency);
extern int vosk_recognizer_accept_waveform(VoskRecognizer *recognizer, const char *data, int length);
extern const char *vosk_recognizer_result(VoskRecognizer *recognizer);
extern const char *vosk_recognizer_final_result(VoskRecognizer *recognizer);
extern void vosk_recognizer_set_words(VoskRecognizer *recognizer, int words);
extern void vosk_recognizer_free(VoskRecognizer *recognizer);

#define DEFAULT_DEVICE "/dev/input/by-path/platform-gpio-keys-event"
#define DEFAULT_UINPUT "/dev/uinput"
#define DEFAULT_MODEL "/home/steamos/voice-dictation/models/vosk-model-small-en-us-0.15"
#define DEFAULT_START_SOUND "/home/steamos/.local/share/Steam/steamui/sounds/recording_start.wav"
#define DEFAULT_END_SOUND "/home/steamos/.local/share/Steam/steamui/sounds/recording_stop.wav"
#define DEFAULT_TRIGGER_KEY 353

static volatile sig_atomic_t g_stop = 0;
static void on_signal(int sig) { (void)sig; g_stop = 1; }

static double monotonic_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void msleep(int ms) {
    struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

static void play_sound(const char *path, bool wait_for_it) {
    if (!path || !*path || access(path, R_OK) != 0) return;
    pid_t pid = fork();
    if (pid == 0) {
        int nullfd = open("/dev/null", O_WRONLY);
        if (nullfd >= 0) { dup2(nullfd, STDOUT_FILENO); dup2(nullfd, STDERR_FILENO); close(nullfd); }
        execlp("pw-play", "pw-play", path, (char *)NULL);
        _exit(127);
    }
    if (pid > 0 && wait_for_it) {
        int st;
        waitpid(pid, &st, 0);
    }
}

struct bytes {
    uint8_t *p;
    size_t n, cap;
};
static void bytes_free(struct bytes *b) { free(b->p); b->p = NULL; b->n = b->cap = 0; }
static int bytes_append(struct bytes *b, const void *p, size_t n) {
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
static void bytes_clear(struct bytes *b) { b->n = 0; }

typedef struct seg_node { struct bytes pcm; bool append_return; struct seg_node *next; } seg_node;
typedef struct text_node { char *text; struct text_node *next; } text_node;

typedef struct {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    seg_node *head, *tail;
    bool done;
} seg_queue;

typedef struct {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    text_node *head, *tail;
    bool done;
} text_queue;

static void segq_init(seg_queue *q) { memset(q,0,sizeof(*q)); pthread_mutex_init(&q->mu,NULL); pthread_cond_init(&q->cv,NULL); }
static void textq_init(text_queue *q) { memset(q,0,sizeof(*q)); pthread_mutex_init(&q->mu,NULL); pthread_cond_init(&q->cv,NULL); }
static void segq_push(seg_queue *q, struct bytes *pcm, bool append_return) {
    seg_node *n = calloc(1,sizeof(*n)); if (!n) return;
    n->pcm = *pcm; n->append_return = append_return; pcm->p=NULL; pcm->n=pcm->cap=0;
    pthread_mutex_lock(&q->mu);
    if (q->tail) q->tail->next = n; else q->head = n; q->tail = n;
    pthread_cond_signal(&q->cv); pthread_mutex_unlock(&q->mu);
}
static bool segq_pop(seg_queue *q, struct bytes *out, bool *append_return) {
    pthread_mutex_lock(&q->mu);
    while (!q->head && !q->done) pthread_cond_wait(&q->cv,&q->mu);
    if (!q->head) { pthread_mutex_unlock(&q->mu); return false; }
    seg_node *n=q->head; q->head=n->next; if(!q->head) q->tail=NULL;
    pthread_mutex_unlock(&q->mu);
    *out=n->pcm; *append_return=n->append_return; free(n); return true;
}
static void textq_push(text_queue *q, char *text) {
    text_node *n = calloc(1,sizeof(*n)); if (!n) { free(text); return; }
    n->text=text;
    pthread_mutex_lock(&q->mu);
    if(q->tail) q->tail->next=n; else q->head=n; q->tail=n;
    pthread_cond_signal(&q->cv); pthread_mutex_unlock(&q->mu);
}
static bool textq_pop(text_queue *q, char **out) {
    pthread_mutex_lock(&q->mu);
    while(!q->head && !q->done) pthread_cond_wait(&q->cv,&q->mu);
    if(!q->head) { pthread_mutex_unlock(&q->mu); return false; }
    text_node *n=q->head; q->head=n->next; if(!q->head) q->tail=NULL;
    pthread_mutex_unlock(&q->mu);
    *out=n->text; free(n); return true;
}
static void segq_done(seg_queue *q){ pthread_mutex_lock(&q->mu); q->done=true; pthread_cond_broadcast(&q->cv); pthread_mutex_unlock(&q->mu); }
static void textq_done(text_queue *q){ pthread_mutex_lock(&q->mu); q->done=true; pthread_cond_broadcast(&q->cv); pthread_mutex_unlock(&q->mu); }

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

struct app;
typedef struct app {
    const char *device, *uinput, *model_path, *source, *start_sound, *end_sound;
    int key, rate, chunk_bytes;
    double actionable_pause, final_pause, silence_threshold, no_speech_timeout, preroll, warmup;
    bool no_sounds, dry_run;
    int ufd;
    VoskModel *model;
    seg_queue segq;
    text_queue textq;
} app;

static double pcm_rms(const uint8_t *data, size_t n) {
    size_t samples = n / 2; if(!samples) return 0.0;
    const int16_t *s = (const int16_t*)data;
    double sum = 0.0;
    for(size_t i=0;i<samples;i++) sum += (double)s[i]*(double)s[i];
    return sqrt(sum / (double)samples);
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

static void *segment_thread_main(void *vp) {
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

static int emit_ev(int fd, uint16_t type, uint16_t code, int32_t value) {
    struct input_event ev; memset(&ev,0,sizeof(ev)); ev.type=type; ev.code=code; ev.value=value;
    return write(fd,&ev,sizeof(ev)) == sizeof(ev) ? 0 : -1;
}
static void key_ev(int fd, int code, bool down) { emit_ev(fd,EV_KEY,code,down?1:0); emit_ev(fd,EV_SYN,SYN_REPORT,0); usleep(8000); }
static void tap(int fd, int code, bool shift) { if(shift) key_ev(fd,KEY_LEFTSHIFT,true); key_ev(fd,code,true); key_ev(fd,code,false); if(shift) key_ev(fd,KEY_LEFTSHIFT,false); }

typedef struct { char ch; int key; bool shift; } keymap;
static keymap maps[] = {
 {'a',KEY_A,false},{'b',KEY_B,false},{'c',KEY_C,false},{'d',KEY_D,false},{'e',KEY_E,false},{'f',KEY_F,false},{'g',KEY_G,false},{'h',KEY_H,false},{'i',KEY_I,false},{'j',KEY_J,false},{'k',KEY_K,false},{'l',KEY_L,false},{'m',KEY_M,false},{'n',KEY_N,false},{'o',KEY_O,false},{'p',KEY_P,false},{'q',KEY_Q,false},{'r',KEY_R,false},{'s',KEY_S,false},{'t',KEY_T,false},{'u',KEY_U,false},{'v',KEY_V,false},{'w',KEY_W,false},{'x',KEY_X,false},{'y',KEY_Y,false},{'z',KEY_Z,false},
 {'A',KEY_A,true},{'B',KEY_B,true},{'C',KEY_C,true},{'D',KEY_D,true},{'E',KEY_E,true},{'F',KEY_F,true},{'G',KEY_G,true},{'H',KEY_H,true},{'I',KEY_I,true},{'J',KEY_J,true},{'K',KEY_K,true},{'L',KEY_L,true},{'M',KEY_M,true},{'N',KEY_N,true},{'O',KEY_O,true},{'P',KEY_P,true},{'Q',KEY_Q,true},{'R',KEY_R,true},{'S',KEY_S,true},{'T',KEY_T,true},{'U',KEY_U,true},{'V',KEY_V,true},{'W',KEY_W,true},{'X',KEY_X,true},{'Y',KEY_Y,true},{'Z',KEY_Z,true},
 {'1',KEY_1,false},{'2',KEY_2,false},{'3',KEY_3,false},{'4',KEY_4,false},{'5',KEY_5,false},{'6',KEY_6,false},{'7',KEY_7,false},{'8',KEY_8,false},{'9',KEY_9,false},{'0',KEY_0,false},
 {'!',KEY_1,true},{'@',KEY_2,true},{'#',KEY_3,true},{'$',KEY_4,true},{'%',KEY_5,true},{'^',KEY_6,true},{'&',KEY_7,true},{'*',KEY_8,true},{'(',KEY_9,true},{')',KEY_0,true},
 {' ',KEY_SPACE,false},{'\n',KEY_ENTER,false},{'-',KEY_MINUS,false},{'_',KEY_MINUS,true},{'=',KEY_EQUAL,false},{'+',KEY_EQUAL,true},{'[',KEY_LEFTBRACE,false},{'{',KEY_LEFTBRACE,true},{']',KEY_RIGHTBRACE,false},{'}',KEY_RIGHTBRACE,true},{'\\',KEY_BACKSLASH,false},{'|',KEY_BACKSLASH,true},{';',KEY_SEMICOLON,false},{':',KEY_SEMICOLON,true},{'\'',KEY_APOSTROPHE,false},{'"',KEY_APOSTROPHE,true},{'`',KEY_GRAVE,false},{'~',KEY_GRAVE,true},{',',KEY_COMMA,false},{'<',KEY_COMMA,true},{'.',KEY_DOT,false},{'>',KEY_DOT,true},{'/',KEY_SLASH,false},{'?',KEY_SLASH,true}
};
static void type_text(int fd, const char *s) {
    for(const unsigned char *p=(const unsigned char*)s; *p; p++) {
        bool found=false;
        for(size_t i=0;i<sizeof(maps)/sizeof(maps[0]);i++) if(maps[i].ch==(char)*p) { tap(fd,maps[i].key,maps[i].shift); found=true; break; }
        if(!found) fprintf(stderr,"skip char 0x%02x\n", *p);
    }
}
static void *type_thread_main(void *vp) {
    app *a=vp; char *text=NULL;
    while(textq_pop(&a->textq, &text)) {
        fprintf(stderr,"type: '%s'\n", text);
        if(!a->dry_run && a->ufd>=0) type_text(a->ufd, text);
        free(text);
    }
    return NULL;
}

static int setup_uinput(const char *path) {
    int fd=open(path,O_WRONLY|O_NONBLOCK); if(fd<0) return -1;
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    int keys[] = {KEY_LEFTSHIFT,KEY_SPACE,KEY_ENTER,KEY_MINUS,KEY_EQUAL,KEY_LEFTBRACE,KEY_RIGHTBRACE,KEY_BACKSLASH,KEY_SEMICOLON,KEY_APOSTROPHE,KEY_GRAVE,KEY_COMMA,KEY_DOT,KEY_SLASH,KEY_1,KEY_2,KEY_3,KEY_4,KEY_5,KEY_6,KEY_7,KEY_8,KEY_9,KEY_0,KEY_A,KEY_B,KEY_C,KEY_D,KEY_E,KEY_F,KEY_G,KEY_H,KEY_I,KEY_J,KEY_K,KEY_L,KEY_M,KEY_N,KEY_O,KEY_P,KEY_Q,KEY_R,KEY_S,KEY_T,KEY_U,KEY_V,KEY_W,KEY_X,KEY_Y,KEY_Z};
    for(size_t i=0;i<sizeof(keys)/sizeof(keys[0]);i++) ioctl(fd, UI_SET_KEYBIT, keys[i]);
    struct uinput_setup us; memset(&us,0,sizeof(us)); snprintf(us.name,sizeof(us.name),"frame-dictate-native"); us.id.bustype=BUS_USB; us.id.vendor=0x28de; us.id.product=0xd1c7; us.id.version=1;
    if(ioctl(fd, UI_DEV_SETUP, &us)<0 || ioctl(fd, UI_DEV_CREATE)<0) { close(fd); return -1; }
    msleep(200); return fd;
}

static pid_t start_pw_record(app *a, int *outfd) {
    int pipefd[2]; if(pipe(pipefd)<0) return -1;
    pid_t pid=fork();
    if(pid==0) {
        dup2(pipefd[1], STDOUT_FILENO); close(pipefd[0]); close(pipefd[1]);
        int nullfd=open("/dev/null",O_WRONLY); if(nullfd>=0){ dup2(nullfd, STDERR_FILENO); close(nullfd); }
        char rate[32]; snprintf(rate,sizeof(rate),"%d",a->rate);
        if(a->source && *a->source) execlp("pw-record","pw-record","--rate",rate,"--channels","1","--format","s16","--raw","--target",a->source,"-",(char*)NULL);
        else execlp("pw-record","pw-record","--rate",rate,"--channels","1","--format","s16","--raw","-",(char*)NULL);
        _exit(127);
    }
    close(pipefd[1]); *outfd=pipefd[0]; return pid;
}
static void stop_child(pid_t pid) { if(pid>0){ kill(pid,SIGTERM); int st; waitpid(pid,&st,0); } }

static void run_utterance(app *a, int efd) {
    int afd=-1; pid_t recpid=start_pw_record(a,&afd); if(recpid<0){ perror("pw-record"); return; }
    if(a->warmup>0) usleep((useconds_t)(a->warmup*1000000.0));
    if(!a->no_sounds) play_sound(a->start_sound,true);
    fprintf(stderr,"listening...\n");

    uint8_t *buf=malloc(a->chunk_bytes); if(!buf){ close(afd); stop_child(recpid); return; }
    struct bytes segment={0}, preroll={0};
    size_t preroll_max=(size_t)(a->preroll * a->rate * 2.0);
    bool in_segment=false, speech_seen=false; double last_voice=0, started=monotonic_s();
    while(!g_stop) {
        struct pollfd pfds[2] = {{.fd=afd,.events=POLLIN},{.fd=efd,.events=POLLIN}};
        int pr = poll(pfds, 2, -1);
        if (pr < 0) { if(errno==EINTR) continue; break; }
        if (pfds[1].revents & POLLIN) {
            struct input_event ev;
            if (read(efd, &ev, sizeof(ev)) == sizeof(ev)) {
                if (ev.type == EV_KEY && ev.code == a->key && ev.value == 1) {
                    fprintf(stderr,"aux pressed during dictation: end now + return\n");
                    if(in_segment && segment.n) {
                        fprintf(stderr,"commit (aux-return): %.2fs audio\n", (double)segment.n/2.0/(double)a->rate);
                        segq_push(&a->segq,&segment,true);
                    } else {
                        struct bytes empty={0};
                        segq_push(&a->segq,&empty,true);
                    }
                    goto done_recording;
                }
            }
        }
        if (!(pfds[0].revents & POLLIN)) continue;
        ssize_t nr=read(afd,buf,a->chunk_bytes); if(nr<=0) break;
        double now=monotonic_s(); double rms=pcm_rms(buf,(size_t)nr); bool voiced=rms>=a->silence_threshold;
        if(!in_segment && preroll_max) {
            bytes_append(&preroll,buf,(size_t)nr);
            if(preroll.n > preroll_max) {
                size_t drop=preroll.n-preroll_max;
                memmove(preroll.p,preroll.p+drop,preroll.n-drop); preroll.n-=drop;
            }
        }
        if(voiced) {
            speech_seen=true; last_voice=now;
            if(!in_segment) { in_segment=true; bytes_clear(&segment); bytes_append(&segment,preroll.p,preroll.n); bytes_clear(&preroll); }
        }
        if(in_segment) bytes_append(&segment,buf,(size_t)nr);
        if(in_segment && now-last_voice >= a->actionable_pause) {
            fprintf(stderr,"commit (%.1fs actionable pause): %.2fs audio\n", a->actionable_pause, (double)segment.n/2.0/(double)a->rate);
            segq_push(&a->segq,&segment,false); in_segment=false; bytes_clear(&preroll);
        }
        if(speech_seen && now-last_voice >= a->final_pause) { fprintf(stderr,"final pause detected (%.1fs)\n", a->final_pause); break; }
        if(!speech_seen && now-started >= a->no_speech_timeout) { fprintf(stderr,"no speech detected\n"); break; }
    }
done_recording:
    if(in_segment && segment.n) { fprintf(stderr,"commit (final): %.2fs audio\n", (double)segment.n/2.0/(double)a->rate); segq_push(&a->segq,&segment,false); }
    if(!a->no_sounds) play_sound(a->end_sound,false);
    bytes_free(&segment); bytes_free(&preroll); free(buf); close(afd); stop_child(recpid);
}

static void usage(const char *argv0) {
    fprintf(stderr,"Usage: %s [options]\n", argv0);
    fprintf(stderr,"  --actionable-pause-seconds N  default 0.35\n  --pause-seconds N             default 3.0\n  --chunk-bytes N               default 1600\n  --preroll-seconds N           default 0.3\n  --recorder-warmup-seconds N   default 0.2\n  --silence-threshold N         default 100\n  --dry-run | --no-sounds\n");
}

int main(int argc, char **argv) {
    app a = {.device=DEFAULT_DEVICE,.uinput=DEFAULT_UINPUT,.model_path=DEFAULT_MODEL,.source=NULL,.start_sound=DEFAULT_START_SOUND,.end_sound=DEFAULT_END_SOUND,.key=DEFAULT_TRIGGER_KEY,.rate=16000,.chunk_bytes=1600,.actionable_pause=0.35,.final_pause=3.0,.silence_threshold=100.0,.no_speech_timeout=8.0,.preroll=0.3,.warmup=0.2,.ufd=-1};
    for(int i=1;i<argc;i++) {
        #define NEEDVAL() if(i+1>=argc){usage(argv[0]); return 2;}
        if(!strcmp(argv[i],"--device")){NEEDVAL(); a.device=argv[++i];}
        else if(!strcmp(argv[i],"--uinput")){NEEDVAL(); a.uinput=argv[++i];}
        else if(!strcmp(argv[i],"--model")){NEEDVAL(); a.model_path=argv[++i];}
        else if(!strcmp(argv[i],"--source")){NEEDVAL(); a.source=argv[++i];}
        else if(!strcmp(argv[i],"--key")){NEEDVAL(); a.key=atoi(argv[++i]);}
        else if(!strcmp(argv[i],"--rate")){NEEDVAL(); a.rate=atoi(argv[++i]);}
        else if(!strcmp(argv[i],"--chunk-bytes")){NEEDVAL(); a.chunk_bytes=atoi(argv[++i]);}
        else if(!strcmp(argv[i],"--actionable-pause-seconds")){NEEDVAL(); a.actionable_pause=atof(argv[++i]);}
        else if(!strcmp(argv[i],"--pause-seconds")){NEEDVAL(); a.final_pause=atof(argv[++i]);}
        else if(!strcmp(argv[i],"--silence-threshold")){NEEDVAL(); a.silence_threshold=atof(argv[++i]);}
        else if(!strcmp(argv[i],"--no-speech-timeout")){NEEDVAL(); a.no_speech_timeout=atof(argv[++i]);}
        else if(!strcmp(argv[i],"--preroll-seconds")){NEEDVAL(); a.preroll=atof(argv[++i]);}
        else if(!strcmp(argv[i],"--recorder-warmup-seconds")){NEEDVAL(); a.warmup=atof(argv[++i]);}
        else if(!strcmp(argv[i],"--start-sound")){NEEDVAL(); a.start_sound=argv[++i];}
        else if(!strcmp(argv[i],"--end-sound")){NEEDVAL(); a.end_sound=argv[++i];}
        else if(!strcmp(argv[i],"--dry-run")) a.dry_run=true;
        else if(!strcmp(argv[i],"--no-sounds")) a.no_sounds=true;
        else if(!strcmp(argv[i],"--help")||!strcmp(argv[i],"-h")){usage(argv[0]); return 0;}
        else if(!strcmp(argv[i],"--continuous")) {}
        else { fprintf(stderr,"unknown option: %s\n",argv[i]); usage(argv[0]); return 2; }
    }
    signal(SIGINT,on_signal); signal(SIGTERM,on_signal);
    if(!a.dry_run) { a.ufd=setup_uinput(a.uinput); if(a.ufd<0){perror("uinput"); return 1;} }
    vosk_set_log_level(-1);
    fprintf(stderr,"Loading Vosk model once: %s\n", a.model_path);
    a.model=vosk_model_new(a.model_path); if(!a.model){fprintf(stderr,"failed to load model\n"); return 1;}
    fprintf(stderr,"Ready. Press aux/side to start; short pauses commit silently, long pause stops. Press aux while dictating to stop early and send Return.\n");
    if(!a.no_sounds){ fprintf(stderr,"start sound: %s\nend sound:   %s\n",a.start_sound,a.end_sound); }
    segq_init(&a.segq); textq_init(&a.textq);
    pthread_t st, tt; pthread_create(&st,NULL,segment_thread_main,&a); pthread_create(&tt,NULL,type_thread_main,&a);
    int efd=open(a.device,O_RDONLY); if(efd<0){perror("open input"); return 1;}
    int one=1; ioctl(efd, EVIOCGRAB, &one);
    struct input_event ev;
    while(!g_stop && read(efd,&ev,sizeof(ev))==sizeof(ev)) {
        if(ev.type==EV_KEY && ev.code==a.key && ev.value==1) run_utterance(&a, efd);
    }
    one=0; ioctl(efd, EVIOCGRAB, &one); close(efd);
    segq_done(&a.segq); pthread_join(st,NULL); pthread_join(tt,NULL);
    if(a.ufd>=0){ ioctl(a.ufd, UI_DEV_DESTROY); close(a.ufd); }
    vosk_model_free(a.model);
    return 0;
}
