#pragma once
#include "queue.h"
#include "display_list.h"
#include "upload_registry.h"
#include "event_registry.h"
#include <stdbool.h>
#include <pthread.h>

typedef struct {
    int                  port;
    CmdQueue            *queue;
    DisplayListRegistry *dl_registry; /* may be NULL */
    UploadRegistry      *ur_registry; /* may be NULL */
    EventRegistry       *ev_registry; /* may be NULL */

    int              listen_fd;
    volatile bool    running;
    pthread_t        thread;
} ServerState;

// Prepare the server state.  Does not open any sockets.
void server_init(ServerState *s, int port, CmdQueue *queue,
                 DisplayListRegistry *dl_registry,
                 UploadRegistry      *ur_registry,
                 EventRegistry       *ev_registry);

// Bind, listen, and start the listener thread.  Returns false on error.
bool server_start(ServerState *s);

// Signal shutdown, close the listen socket, and join the listener thread.
void server_stop(ServerState *s);

// Returns the current number of active client connections.
int server_get_active_clients(void);
