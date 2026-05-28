#include "queue.h"
#include "protocol.h"
#include "rls_log.h"
#include <stdlib.h>
#include <string.h>

void cmdq_init(CmdQueue *q) {
    memset(q, 0, sizeof(*q));
    pthread_mutex_init(&q->mu, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);
}

void cmdq_destroy(CmdQueue *q) {
    pthread_mutex_lock(&q->mu);
    q->shutdown = true;
    // Free remaining entries.
    while (q->count > 0) {
        CmdEntry *e = &q->items[q->head];
        if (e->parsed) {
            protocol_free(e->parsed);
            e->parsed = NULL;
        }
        q->head = (q->head + 1) % CMD_QUEUE_CAPACITY;
        q->count--;
    }
    pthread_cond_broadcast(&q->not_empty);
    pthread_cond_broadcast(&q->not_full);
    pthread_mutex_unlock(&q->mu);

    pthread_mutex_destroy(&q->mu);
    pthread_cond_destroy(&q->not_empty);
    pthread_cond_destroy(&q->not_full);
}

bool cmdq_push(CmdQueue *q, CmdEntry e) {
    pthread_mutex_lock(&q->mu);
    if (q->count >= CMD_QUEUE_CAPACITY && !q->shutdown) {
        RLS_WARNING("command queue full (%d/%d), blocking client thread",
                    q->count, CMD_QUEUE_CAPACITY);
        while (q->count >= CMD_QUEUE_CAPACITY && !q->shutdown)
            pthread_cond_wait(&q->not_full, &q->mu);
    }
    if (q->shutdown) {
        pthread_mutex_unlock(&q->mu);
        return false;
    }
    q->items[q->tail] = e;
    q->tail = (q->tail + 1) % CMD_QUEUE_CAPACITY;
    q->count++;
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->mu);
    return true;
}

bool cmdq_pop(CmdQueue *q, CmdEntry *out) {
    pthread_mutex_lock(&q->mu);
    if (q->count == 0) {
        pthread_mutex_unlock(&q->mu);
        return false;
    }
    *out = q->items[q->head];
    q->head = (q->head + 1) % CMD_QUEUE_CAPACITY;
    q->count--;
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->mu);
    return true;
}

int cmdq_count(CmdQueue *q) {
    pthread_mutex_lock(&q->mu);
    int c = q->count;
    pthread_mutex_unlock(&q->mu);
    return c;
}
