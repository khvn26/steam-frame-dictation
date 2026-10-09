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
#include <sys/wait.h>
#include <unistd.h>

volatile sig_atomic_t g_stop = 0;
static void on_signal(int sig) { (void)sig; g_stop = 1; }

static void quiet_log_callback(enum ggml_log_level level, const char *text, void *user_data) {
    (void)level; (void)text; (void)user_data;
}

static void enable_quiet_mode(void) {
    whisper_log_set(quiet_log_callback, NULL);
    int fd = open("/dev/null", O_WRONLY);
    if (fd >= 0) {
        dup2(fd, STDERR_FILENO);
        if (fd != STDERR_FILENO) close(fd);
    }
}

static int run_command(char *const argv[]) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        execvp(argv[0], argv);
        _exit(127);
    }
    int st = 0;
    if (waitpid(pid, &st, 0) < 0) return -1;
    return (WIFEXITED(st) && WEXITSTATUS(st) == 0) ? 0 : -1;
}

static int uninstall_user_service(void) {
    const char *home = getenv("HOME");
    if (!home || !*home) {
        fprintf(stderr, "HOME is not set; cannot uninstall\n");
        return 1;
    }

    char service_path[1024];
    char install_dir[1024];
    snprintf(service_path, sizeof(service_path), "%s/.config/systemd/user/frame-dictation.service", home);
    snprintf(install_dir, sizeof(install_dir), "%s/.local/share/steam-frame-dictation", home);

    char *disable_args[] = {"systemctl", "--user", "disable", "--now", "frame-dictation.service", NULL};
    run_command(disable_args); // ok if service is not installed/running

    unlink(service_path);

    char *reload_args[] = {"systemctl", "--user", "daemon-reload", NULL};
    run_command(reload_args);

    char *rm_args[] = {"rm", "-rf", install_dir, NULL};
    if (run_command(rm_args) != 0) {
        fprintf(stderr, "warning: failed to remove %s\n", install_dir);
        return 1;
    }

    printf("Uninstalled Steam Frame Dictation.\n");
    return 0;
}

static void usage(FILE *out, const char *argv0) {
    fprintf(out, "Steam Frame Dictation — offline voice typing\nUsage: %s [options]\n\n", argv0);
    fputs(
        "Press aux to start; short pauses commit silently, long pauses stop.\n"
        "Press aux again to stop and send Return after pending text.\n\n"
        "General:\n"
        "  -h, --help                    Show this help and exit\n"
        "  -V, --version                 Print build revision and exit\n"
        "  --uninstall                   Stop/disable service and remove the default\n"
        "                                user install (not the source checkout)\n"
        "  --quiet                       Suppress runtime logs, including errors\n"
        "  --dry-run                     Do not create/type through a virtual keyboard;\n"
        "                                still grabs input, records, and transcribes\n\n"
        "Input and capture:\n"
        "  --device PATH                 Trigger evdev device\n"
        "                                default: " DEFAULT_DEVICE "\n"
        "  --key N                       Trigger key code (default: 353)\n"
        "  --uinput PATH                 Virtual keyboard device (default: " DEFAULT_UINPUT ")\n"
        "  --source NAME                 pw-record target (default: system microphone)\n"
        "  --rate N                      Capture sample rate (default: 16000 Hz)\n"
        "                                Keep at 16000: Whisper expects 16 kHz audio\n"
        "  --chunk-bytes N               PCM read size (default: 1600 bytes)\n"
        "  --preroll-seconds N            Audio retained before speech (default: 0.3)\n"
        "  --recorder-warmup-seconds N    Capture warmup before start chime (default: 0.2)\n\n"
        "Segmentation and filtering:\n"
        "  --actionable-pause-seconds N   Silence before segment commit (default: 0.35)\n"
        "  --pause-seconds N              Silence after speech before stop (default: 3.0)\n"
        "  --no-speech-timeout N          Stop if no speech detected (default: 8.0 seconds)\n"
        "  --silence-threshold N          PCM RMS speech threshold (default: 100)\n"
        "  --min-segment-seconds N        Minimum transcribed segment duration (default: 0.6)\n"
        "  --min-transcribe-rms N         Minimum segment PCM RMS (default: 150)\n\n"
        "Whisper:\n"
        "  --whisper-model PATH          Model file\n"
        "                                default: " DEFAULT_WHISPER_MODEL "\n"
        "  --whisper-threads N            Inference threads (default: 4)\n"
        "  --whisper-audio-ctx N          Audio context size (default: 0 = full)\n"
        "  --whisper-max-tokens N         Segment token limit (default: 0 = unlimited)\n\n"
        "Audio feedback:\n"
        "  --duck-volume N               Playback volume cap while listening\n"
        "                                (default: 0.15; 1.0 = 100%, not a multiplier)\n"
        "  --no-duck                     Disable playback ducking\n"
        "  --start-sound PATH            Start chime (default: " DEFAULT_START_SOUND ")\n"
        "  --end-sound PATH              Stop chime (default: " DEFAULT_END_SOUND ")\n"
        "  --no-sounds                   Disable chimes\n\n"
        "Installed service overrides: threads=2, audio_ctx=768, max_tokens=32,\n"
        "--quiet, and an explicit installed model path. Other defaults are above.\n"
        "Without --quiet, transcripts are logged. Stop the service before running\n"
        "a second instance. Numeric values must be sensible; validation is limited.\n",
        out);
}

static app default_app(void) {
    return (app){.device=DEFAULT_DEVICE,.uinput=DEFAULT_UINPUT,.source=NULL,.start_sound=DEFAULT_START_SOUND,.end_sound=DEFAULT_END_SOUND,.whisper_model=DEFAULT_WHISPER_MODEL,.key=DEFAULT_TRIGGER_KEY,.rate=16000,.chunk_bytes=1600,.whisper_threads=4,.whisper_audio_ctx=0,.whisper_max_tokens=0,.actionable_pause=0.35,.final_pause=3.0,.silence_threshold=100.0,.no_speech_timeout=8.0,.preroll=0.3,.warmup=0.2,.min_segment_seconds=0.6,.min_transcribe_rms=150.0,.duck_volume=0.15,.duck_enabled=true,.ufd=-1};
}

int main(int argc, char **argv) {
    app a = default_app();
    for(int i=1;i<argc;i++) {
        #define NEEDVAL() if(i+1>=argc){usage(stderr, argv[0]); return 2;}
        if(!strcmp(argv[i],"--whisper-model")){NEEDVAL(); a.whisper_model=argv[++i];}
        else if(!strcmp(argv[i],"--whisper-threads")){NEEDVAL(); a.whisper_threads=atoi(argv[++i]);}
        else if(!strcmp(argv[i],"--whisper-audio-ctx")){NEEDVAL(); a.whisper_audio_ctx=atoi(argv[++i]);}
        else if(!strcmp(argv[i],"--whisper-max-tokens")){NEEDVAL(); a.whisper_max_tokens=atoi(argv[++i]);}
        else if(!strcmp(argv[i],"--device")){NEEDVAL(); a.device=argv[++i];}
        else if(!strcmp(argv[i],"--uinput")){NEEDVAL(); a.uinput=argv[++i];}
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
        else if(!strcmp(argv[i],"--quiet")) a.quiet=true;
        else if(!strcmp(argv[i],"--no-sounds")) a.no_sounds=true;
        else if(!strcmp(argv[i],"--version")||!strcmp(argv[i],"-V")){printf("steam-frame-dictation %s\n", GIT_REVISION); return 0;}
        else if(!strcmp(argv[i],"--uninstall")){return uninstall_user_service();}
        else if(!strcmp(argv[i],"--help")||!strcmp(argv[i],"-h")){usage(stdout, argv[0]); return 0;}
        else { fprintf(stderr,"unknown option: %s\n",argv[i]); usage(stderr, argv[0]); return 2; }
    }
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    if (a.quiet) enable_quiet_mode();
    if(!a.dry_run) { a.ufd=setup_uinput(a.uinput); if(a.ufd<0){perror("uinput"); return 1;} }
    fprintf(stderr,"steam-frame-dictation %s\n", GIT_REVISION);
    fprintf(stderr,"provider: libwhisper\n");
    fprintf(stderr,"Loading Whisper model once: %s\n", a.whisper_model);
    fprintf(stderr,"whisper settings: threads=%d audio_ctx=%d max_tokens=%d\n", a.whisper_threads, a.whisper_audio_ctx, a.whisper_max_tokens);
    struct whisper_context_params cparams = whisper_context_default_params();
    a.whisper_ctx = whisper_init_from_file_with_params(a.whisper_model, cparams);
    if(!a.whisper_ctx){fprintf(stderr,"failed to load whisper model\n"); return 1;}
    fprintf(stderr,"Ready. Press aux/side to start; short pauses commit silently, long pause stops. Press aux while dictating to stop early and send Return.\n");
    if(!a.no_sounds){ fprintf(stderr,"start sound: %s\nend sound:   %s\n",a.start_sound,a.end_sound); }
    fprintf(stderr,"segment filter: min %.2fs, rms %.0f\n", a.min_segment_seconds, a.min_transcribe_rms);
    fprintf(stderr,"text logging: %s\n", a.quiet ? "disabled" : "enabled");
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
    return 0;
}
