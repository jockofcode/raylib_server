#pragma once
#include <stdbool.h>
#include <pthread.h>

#define CMD_QUEUE_CAPACITY 2048

// Forward declaration so queue.h doesn't pull in cJSON.
typedef struct ParsedCmd ParsedCmd;

typedef struct {
    ParsedCmd *parsed;    // heap-allocated; consumer frees via protocol_free()
    int        conn_fd;   // originating socket fd (-1 if none)
    int        client_id; // connection identifier
} CmdEntry;

typedef struct {
    CmdEntry        items[CMD_QUEUE_CAPACITY];
    int             head;
    int             tail;
    int             count;
    bool            shutdown;
    pthread_mutex_t mu;
    pthread_cond_t  not_empty;
    pthread_cond_t  not_full;
} CmdQueue;

void cmdq_init(CmdQueue *q);

// Signal shutdown and wake any blocked waiters, then free remaining entries.
void cmdq_destroy(CmdQueue *q);

// Push an entry; blocks when full.
// Returns false if the queue has been shut down (entry is NOT consumed).
bool cmdq_push(CmdQueue *q, CmdEntry e);

// Pop an entry; non-blocking.
// Returns false if empty.
bool cmdq_pop(CmdQueue *q, CmdEntry *out);

// Approximate count (not synchronized with respect to other operations).
int cmdq_count(CmdQueue *q);
