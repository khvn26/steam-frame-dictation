#include "app.h"
#include "whisper.h"
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

volatile sig_atomic_t g_stop = 0;
static void on_signal(int sig) { (void)sig; g_stop = 1; }

static void usage(const char *argv0) {
    fprintf(stderr,"Usage: %s [options]\n", argv0);
    fprintf(stderr,"  --version                    print build git revision\n");
    fprintf(stderr,"  --provider NAME               vosk (default), whisper, or whisper-cli\n  --whisper-cli PATH            default " DEFAULT_WHISPER_CLI "\n  --whisper-model PATH          default " DEFAULT_WHISPER_MODEL "\n  --whisper-threads N           default 4\n  --whisper-audio-ctx N         default 0 (full)\n  --whisper-max-tokens N        default 0 (unlimited)\n  --actionable-pause-seconds N  default 0.35\n  --pause-seconds N             default 3.0\n  --chunk-bytes N               default 1600\n  --preroll-seconds N           default 0.3\n  --recorder-warmup-seconds N   default 0.2\n  --silence-threshold N         default 100\n  --min-segment-seconds N       default 0.6\n  --min-transcribe-rms N        default 150\n  --duck-volume N               default 0.15\n  --no-duck                     disable playback ducking\n  --dry-run | --no-sounds\n");
}

static app default_app(void) {
    return (app){.device=DEFAULT_DEVICE,.uinput=DEFAULT_UINPUT,.model_path=DEFAULT_MODEL,.source=NULL,.start_sound=DEFAULT_START_SOUND,.end_sound=DEFAULT_END_SOUND,.provider="vosk",.whisper_cli=DEFAULT_WHISPER_CLI,.whisper_model=DEFAULT_WHISPER_MODEL,.key=DEFAULT_TRIGGER_KEY,.rate=16000,.chunk_bytes=1600,.whisper_threads=4,.whisper_audio_ctx=0,.whisper_max_tokens=0,.actionable_pause=0.35,.final_pause=3.0,.silence_threshold=100.0,.no_speech_timeout=8.0,.preroll=0.3,.warmup=0.2,.min_segment_seconds=0.6,.min_transcribe_rms=150.0,.duck_volume=0.15,.duck_enabled=true,.ufd=-1};
}

int main(int argc, char **argv) {
    app a = default_app();
    for(int i=1;i<argc;i++) {
        #define NEEDVAL() if(i+1>=argc){usage(argv[0]); return 2;}
        if(!strcmp(argv[i],"--provider")){NEEDVAL(); a.provider=argv[++i];}
        else if(!strcmp(argv[i],"--whisper-cli")){NEEDVAL(); a.whisper_cli=argv[++i];}
        else if(!strcmp(argv[i],"--whisper-model")){NEEDVAL(); a.whisper_model=argv[++i];}
        else if(!strcmp(argv[i],"--whisper-threads")){NEEDVAL(); a.whisper_threads=atoi(argv[++i]);}
        else if(!strcmp(argv[i],"--whisper-audio-ctx")){NEEDVAL(); a.whisper_audio_ctx=atoi(argv[++i]);}
        else if(!strcmp(argv[i],"--whisper-max-tokens")){NEEDVAL(); a.whisper_max_tokens=atoi(argv[++i]);}
        else if(!strcmp(argv[i],"--device")){NEEDVAL(); a.device=argv[++i];}
        else if(!strcmp(argv[i],"--uinput")){NEEDVAL(); a.uinput=argv[++i];}
        else if(!strcmp(argv[i],"--model")){NEEDVAL(); a.model_path=argv[++i];}
        else if(!strcmp(argv[i],"--source")){NEEDVAL(); a.source=argv[++i];}
        else if(!strcmp(argv[i],"--key")){NEEDVAL(); a.key=atoi(argv[++i]);}
        else if(!strcmp(argv[i],"--rate")){NEEDVAL(); a.rate=atoi(argv[++i]);}
        else if(!strcmp(argv[i],"--chunk-bytes")){NEEDVAL(); a.chunk_bytes=atoi(argv[++i]);}
        else if(!strcmp(argv[i],"--actionable-pause-seconds")){NEEDVAL(); a.actionable_pause=atof(argv[++i]);}
        else if(!strcmp(argv[i],"--pause-seconds")){NEEDVAL(); a.final_pause=atof(argv[++i]);}
        else if(!strcmp(argv[i],"--silence-threshold")){NEEDVAL(); a.silence_threshold=atof(argv[++i]);}
        else if(!strcmp(argv[i],"--min-segment-seconds")){NEEDVAL(); a.min_segment_seconds=atof(argv[++i]);}
        else if(!strcmp(argv[i],"--min-transcribe-rms")){NEEDVAL(); a.min_transcribe_rms=atof(argv[++i]);}
        else if(!strcmp(argv[i],"--no-speech-timeout")){NEEDVAL(); a.no_speech_timeout=atof(argv[++i]);}
        else if(!strcmp(argv[i],"--preroll-seconds")){NEEDVAL(); a.preroll=atof(argv[++i]);}
        else if(!strcmp(argv[i],"--recorder-warmup-seconds")){NEEDVAL(); a.warmup=atof(argv[++i]);}
        else if(!strcmp(argv[i],"--start-sound")){NEEDVAL(); a.start_sound=argv[++i];}
        else if(!strcmp(argv[i],"--end-sound")){NEEDVAL(); a.end_sound=argv[++i];}
        else if(!strcmp(argv[i],"--duck-volume")){NEEDVAL(); a.duck_volume=atof(argv[++i]);}
        else if(!strcmp(argv[i],"--no-duck")) a.duck_enabled=false;
        else if(!strcmp(argv[i],"--dry-run")) a.dry_run=true;
        else if(!strcmp(argv[i],"--no-sounds")) a.no_sounds=true;
        else if(!strcmp(argv[i],"--version")||!strcmp(argv[i],"-V")){printf("frame-dictate-vosk-native %s\n", GIT_REVISION); return 0;}
        else if(!strcmp(argv[i],"--help")||!strcmp(argv[i],"-h")){usage(argv[0]); return 0;}
        else if(!strcmp(argv[i],"--continuous")) {}
        else { fprintf(stderr,"unknown option: %s\n",argv[i]); usage(argv[0]); return 2; }
    }
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    if(!a.dry_run) { a.ufd=setup_uinput(a.uinput); if(a.ufd<0){perror("uinput"); return 1;} }
    vosk_set_log_level(-1);
    fprintf(stderr,"frame-dictate-vosk-native %s\n", GIT_REVISION);
    if(!strcmp(a.provider, "vosk")) {
        fprintf(stderr,"provider: vosk\n");
        fprintf(stderr,"Loading Vosk model once: %s\n", a.model_path);
        a.model=vosk_model_new(a.model_path); if(!a.model){fprintf(stderr,"failed to load model\n"); return 1;}
    } else if(!strcmp(a.provider, "whisper")) {
        fprintf(stderr,"provider: libwhisper\n");
        fprintf(stderr,"Loading Whisper model once: %s\n", a.whisper_model);
        fprintf(stderr,"whisper settings: threads=%d audio_ctx=%d max_tokens=%d\n", a.whisper_threads, a.whisper_audio_ctx, a.whisper_max_tokens);
        struct whisper_context_params cparams = whisper_context_default_params();
        a.whisper_ctx = whisper_init_from_file_with_params(a.whisper_model, cparams);
        if(!a.whisper_ctx){fprintf(stderr,"failed to load whisper model\n"); return 1;}
    } else if(!strcmp(a.provider, "whisper-cli")) {
        fprintf(stderr,"provider: whisper-cli\n");
        fprintf(stderr,"whisper cli: %s\nwhisper model: %s\n", a.whisper_cli, a.whisper_model);
    } else {
        fprintf(stderr,"unknown provider: %s\n", a.provider);
        return 2;
    }
    fprintf(stderr,"Ready. Press aux/side to start; short pauses commit silently, long pause stops. Press aux while dictating to stop early and send Return.\n");
    if(!a.no_sounds){ fprintf(stderr,"start sound: %s\nend sound:   %s\n",a.start_sound,a.end_sound); }
    fprintf(stderr,"segment filter: min %.2fs, rms %.0f\n", a.min_segment_seconds, a.min_transcribe_rms);
    fprintf(stderr,"playback ducking: %s", a.duck_enabled ? "enabled" : "disabled");
    if (a.duck_enabled) fprintf(stderr," (volume %.2f)", a.duck_volume);
    fprintf(stderr,"\n");
    segq_init(&a.segq); textq_init(&a.textq);
    pthread_t st, tt; pthread_create(&st,NULL,segment_thread_main,&a); pthread_create(&tt,NULL,type_thread_main,&a);
    int efd=open(a.device,O_RDONLY|O_NONBLOCK); if(efd<0){perror("open input"); return 1;}
    int one=1; ioctl(efd, EVIOCGRAB, &one);
    struct input_event ev;
    while(!g_stop) {
        struct pollfd pfd = {.fd = efd, .events = POLLIN};
        int pr = poll(&pfd, 1, 500);
        if (pr < 0) {
            if (errno == EINTR) continue;
            perror("poll input");
            break;
        }
        if (pr == 0 || !(pfd.revents & POLLIN)) continue;
        while (read(efd,&ev,sizeof(ev))==sizeof(ev)) {
            if(ev.type==EV_KEY && ev.code==a.key && ev.value==1) run_utterance(&a, efd);
            if (g_stop) break;
        }
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            perror("read input");
            break;
        }
    }
    one=0; ioctl(efd, EVIOCGRAB, &one); close(efd);
    segq_done(&a.segq); pthread_join(st,NULL); pthread_join(tt,NULL);
    if(a.ufd>=0){ ioctl(a.ufd, UI_DEV_DESTROY); close(a.ufd); }
    if(a.whisper_ctx) whisper_free(a.whisper_ctx);
    if(a.model) vosk_model_free(a.model);
    return 0;
}
