#include "app.h"
#include <stdlib.h>
#include <string.h>

void segq_init(seg_queue *q) { memset(q,0,sizeof(*q)); pthread_mutex_init(&q->mu,NULL); pthread_cond_init(&q->cv,NULL); }
void textq_init(text_queue *q) { memset(q,0,sizeof(*q)); pthread_mutex_init(&q->mu,NULL); pthread_cond_init(&q->cv,NULL); }
void segq_push(seg_queue *q, struct bytes *pcm, bool append_return) {
    seg_node *n = calloc(1,sizeof(*n)); if (!n) return;
    n->pcm = *pcm; n->append_return = append_return; pcm->p=NULL; pcm->n=pcm->cap=0;
    pthread_mutex_lock(&q->mu);
    if (q->tail) q->tail->next = n; else q->head = n; q->tail = n;
    pthread_cond_signal(&q->cv); pthread_mutex_unlock(&q->mu);
}
bool segq_pop(seg_queue *q, struct bytes *out, bool *append_return) {
    pthread_mutex_lock(&q->mu);
    while (!q->head && !q->done) pthread_cond_wait(&q->cv,&q->mu);
    if (!q->head) { pthread_mutex_unlock(&q->mu); return false; }
    seg_node *n=q->head; q->head=n->next; if(!q->head) q->tail=NULL;
    pthread_mutex_unlock(&q->mu);
    *out=n->pcm; *append_return=n->append_return; free(n); return true;
}
void textq_push(text_queue *q, char *text) {
    text_node *n = calloc(1,sizeof(*n)); if (!n) { free(text); return; }
    n->text=text;
    pthread_mutex_lock(&q->mu);
    if(q->tail) q->tail->next=n; else q->head=n; q->tail=n;
    pthread_cond_signal(&q->cv); pthread_mutex_unlock(&q->mu);
}
bool textq_pop(text_queue *q, char **out) {
    pthread_mutex_lock(&q->mu);
    while(!q->head && !q->done) pthread_cond_wait(&q->cv,&q->mu);
    if(!q->head) { pthread_mutex_unlock(&q->mu); return false; }
    text_node *n=q->head; q->head=n->next; if(!q->head) q->tail=NULL;
    pthread_mutex_unlock(&q->mu);
    *out=n->text; free(n); return true;
}
void segq_done(seg_queue *q){ pthread_mutex_lock(&q->mu); q->done=true; pthread_cond_broadcast(&q->cv); pthread_mutex_unlock(&q->mu); }
void textq_done(text_queue *q){ pthread_mutex_lock(&q->mu); q->done=true; pthread_cond_broadcast(&q->cv); pthread_mutex_unlock(&q->mu); }
