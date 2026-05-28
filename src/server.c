#include "server.h"
#include "protocol.h"
#include "queue.h"
#include "rls_log.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

// ---------------------------------------------------------------------------
// Per-connection state
// ---------------------------------------------------------------------------

typedef struct {
    int        fd;
    int        id;
    CmdQueue  *queue;
    LineBuffer linebuf;
} ConnState;

static _Atomic int g_next_client_id = 1;

// ---------------------------------------------------------------------------
// Line callback — called by linebuf_feed for each complete line
// ---------------------------------------------------------------------------

static void on_line(const char *line, void *userdata) {
    ConnState *conn = userdata;

    ParsedCmd *p = protocol_parse_line(line);
    if (!p) {
        RLS_WARNING("client %d: parse error — %.200s", conn->id, line);
        char *err = protocol_error(NULL, "parse error: invalid JSON or missing cmd");
        protocol_send(conn->fd, err);
        free(err);
        return;
    }

    RLS_DEBUG("client %d: queued cmd=%s", conn->id, p->cmd);

    // Sync commands (Load*, Upload*, Get*, Measure*, etc.) need the main
    // thread to execute them before a result is known.  Skip the optimistic
    // ACK; commands_execute() will send the real response.
    // Fire-and-forget commands get an optimistic ACK immediately.
    if (p->id && !protocol_needs_response(p->cmd)) {
        char *ack = protocol_ok(p->id, NULL);
        protocol_send(conn->fd, ack);
        free(ack);
    }

    CmdEntry e = {
        .parsed    = p,
        .conn_fd   = conn->fd,
        .client_id = conn->id,
    };

    if (!cmdq_push(conn->queue, e)) {
        // Queue has been shut down; discard.
        protocol_free(p);
    }
    // p is now owned by the queue entry — do not free here on success.
}

// ---------------------------------------------------------------------------
// Connection thread
// ---------------------------------------------------------------------------

static void *connection_thread(void *arg) {
    ConnState *conn = arg;
    int id = conn->id;
    char buf[4096];

    RLS_INFO("client %d connected (fd=%d)", id, conn->fd);

    while (1) {
        ssize_t n = recv(conn->fd, buf, sizeof(buf), 0);
        if (n <= 0) break;

        if (linebuf_feed(&conn->linebuf, buf, (size_t)n, on_line, conn) < 0) {
            RLS_WARNING("client %d: line too long, dropping connection", id);
            char *err = protocol_error(NULL, "line too long");
            protocol_send(conn->fd, err);
            free(err);
            break;
        }
    }

    RLS_INFO("client %d disconnected", id);
    close(conn->fd);
    free(conn);
    return NULL;
}

// ---------------------------------------------------------------------------
// Listener thread
// ---------------------------------------------------------------------------

static void *listener_thread(void *arg) {
    ServerState *s = arg;

    while (s->running) {
        struct sockaddr_in addr;
        socklen_t addr_len = sizeof(addr);

        int client_fd = accept(s->listen_fd,
                               (struct sockaddr *)&addr, &addr_len);
        if (client_fd < 0) {
            if (!s->running) break;
            if (errno == EINTR) continue;
            RLS_WARNING("accept failed: %s", strerror(errno));
            break;
        }

        ConnState *conn = calloc(1, sizeof(ConnState));
        if (!conn) { close(client_fd); continue; }

        conn->fd    = client_fd;
        conn->id    = atomic_fetch_add(&g_next_client_id, 1);
        conn->queue = s->queue;
        linebuf_init(&conn->linebuf);

        pthread_t t;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&t, &attr, connection_thread, conn) != 0) {
            close(client_fd);
            free(conn);
        }
        pthread_attr_destroy(&attr);
    }

    return NULL;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void server_init(ServerState *s, int port, CmdQueue *queue) {
    memset(s, 0, sizeof(*s));
    s->port      = port;
    s->queue     = queue;
    s->listen_fd = -1;
}

bool server_start(ServerState *s) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons((uint16_t)s->port),
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
    };

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return false;
    }

    if (listen(fd, 16) < 0) {
        close(fd);
        return false;
    }

    s->listen_fd = fd;
    s->running   = true;

    if (pthread_create(&s->thread, NULL, listener_thread, s) != 0) {
        close(fd);
        s->listen_fd = -1;
        s->running   = false;
        return false;
    }

    return true;
}

void server_stop(ServerState *s) {
    s->running = false;
    if (s->listen_fd >= 0) {
        shutdown(s->listen_fd, SHUT_RDWR);
        close(s->listen_fd);
        s->listen_fd = -1;
    }
    pthread_join(s->thread, NULL);
}
