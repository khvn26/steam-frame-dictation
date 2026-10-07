#pragma once
#define _GNU_SOURCE
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <pthread.h>
#include <signal.h>

// Minimal Vosk C API declarations.
typedef struct VoskModel VoskModel;
typedef struct VoskRecognizer VoskRecognizer;
typedef struct whisper_context whisper_context;
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
#define DEFAULT_WHISPER_MODEL "/home/steamos/voice-dictation/models/whisper/ggml-tiny.en.bin"
#define DEFAULT_TRIGGER_KEY 353
#ifndef GIT_REVISION
#define GIT_REVISION "unknown"
#endif

extern volatile sig_atomic_t g_stop;

struct bytes { uint8_t *p; size_t n, cap; };
typedef struct seg_node { struct bytes pcm; bool append_return; struct seg_node *next; } seg_node;
typedef struct text_node { char *text; struct text_node *next; } text_node;
typedef struct { pthread_mutex_t mu; pthread_cond_t cv; seg_node *head, *tail; bool done; } seg_queue;
typedef struct { pthread_mutex_t mu; pthread_cond_t cv; text_node *head, *tail; bool done; } text_queue;

typedef struct app {
    const char *device, *uinput, *model_path, *source, *start_sound, *end_sound;
    const char *provider, *whisper_model;
    int key, rate, chunk_bytes, whisper_threads, whisper_audio_ctx, whisper_max_tokens;
    double actionable_pause, final_pause, silence_threshold, no_speech_timeout, preroll, warmup;
    double min_segment_seconds, min_transcribe_rms;
    double duck_volume, saved_volume;
    bool no_sounds, dry_run, duck_enabled, has_saved_volume;
    int ufd;
    VoskModel *model;
    whisper_context *whisper_ctx;
    seg_queue segq;
    text_queue textq;
} app;

// util
void bytes_free(struct bytes *b);
int bytes_append(struct bytes *b, const void *p, size_t n);
void bytes_clear(struct bytes *b);
double monotonic_s(void);
void msleep(int ms);
double pcm_rms(const uint8_t *data, size_t n);

// queues
void segq_init(seg_queue *q);
void textq_init(text_queue *q);
void segq_push(seg_queue *q, struct bytes *pcm, bool append_return);
bool segq_pop(seg_queue *q, struct bytes *out, bool *append_return);
void textq_push(text_queue *q, char *text);
bool textq_pop(text_queue *q, char **out);
void segq_done(seg_queue *q);
void textq_done(text_queue *q);

// feedback
void play_sound(const char *path, bool wait_for_it);

// output
int setup_uinput(const char *path);
void type_text(int fd, const char *s);
void *type_thread_main(void *vp);

// audio capture
int start_pw_record(app *a, int *outfd);
void stop_child(int pid);

// audio policy
void audio_policy_begin(app *a);
void audio_policy_end(app *a);

// transcriber
void *segment_thread_main(void *vp);

// session controller
void run_utterance(app *a, int efd);
