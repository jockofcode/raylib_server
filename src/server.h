#pragma once
#include "queue.h"
#include <stdbool.h>
#include <pthread.h>

typedef struct {
    int       port;
    CmdQueue *queue;

    int              listen_fd;
    volatile bool    running;
    pthread_t        thread;
} ServerState;

// Prepare the server state.  Does not open any sockets.
void server_init(ServerState *s, int port, CmdQueue *queue);

// Bind, listen, and start the listener thread.  Returns false on error.
bool server_start(ServerState *s);

// Signal shutdown, close the listen socket, and join the listener thread.
void server_stop(ServerState *s);
