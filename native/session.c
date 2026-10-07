#include "app.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void run_utterance(app *a, int efd) {
    int afd=-1; pid_t recpid=start_pw_record(a,&afd); if(recpid<0){ perror("pw-record"); return; }
    if(a->warmup>0) usleep((useconds_t)(a->warmup*1000000.0));
    if(!a->no_sounds) play_sound(a->start_sound,true);
    audio_policy_begin(a);
    fprintf(stderr,"listening...\n");

    uint8_t *buf=malloc(a->chunk_bytes); if(!buf){ close(afd); stop_child(recpid); audio_policy_end(a); return; }
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
    audio_policy_end(a);
    if(!a->no_sounds) play_sound(a->end_sound,false);
    bytes_free(&segment); bytes_free(&preroll); free(buf); close(afd); stop_child(recpid);
}
