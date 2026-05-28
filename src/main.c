#include "queue.h"
#include "protocol.h"
#include "commands.h"
#include "handle_registry.h"
#include "server.h"
#include "rls_log.h"
#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#define DEFAULT_PORT    7878
#define DEFAULT_WIDTH    800
#define DEFAULT_HEIGHT   600
#define DEFAULT_FPS       60

static CmdQueue       g_queue;
static ServerState    g_server;
static HandleRegistry g_registry;

static void handle_sigint(int sig) {
    (void)sig;
    // Setting a flag here is unsafe from a signal handler in C, but for a
    // development server this is acceptable.  The main loop will see
    // WindowShouldClose() return true after the next PollInputEvents().
}

int main(int argc, char **argv) {
    signal(SIGINT, handle_sigint);
    signal(SIGPIPE, SIG_IGN); // ignore broken pipe on socket writes

    int port = DEFAULT_PORT;
    RlsLogLevel log_level = RLS_LOG_INFO;

    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "--log-level") == 0 || strcmp(argv[i], "-l") == 0)
                && i + 1 < argc) {
            log_level = rls_log_level_from_string(argv[++i]);
        } else if (argv[i][0] != '-') {
            port = atoi(argv[i]);
        }
    }

    rls_set_log_level(log_level);

    // Initialise subsystems.
    cmdq_init(&g_queue);
    handle_registry_init(&g_registry);
    commands_init(&g_registry);

    // Start the TCP server on a background thread.
    server_init(&g_server, port, &g_queue);
    if (!server_start(&g_server)) {
        RLS_ERROR("failed to bind port %d", port);
        cmdq_destroy(&g_queue);
        return 1;
    }
    RLS_INFO("listening on 127.0.0.1:%d", port);

    // Silence raylib's verbose INFO output — we only want WARNING and above
    // from the raylib internals; our own logs use rls_log.
    SetTraceLogLevel(LOG_WARNING);

    // Initialise the raylib window on the main thread (required by macOS).
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(DEFAULT_WIDTH, DEFAULT_HEIGHT, "raylib_server");
    SetTargetFPS(DEFAULT_FPS);

    RLS_INFO("window initialized (%dx%d @ %d fps)", DEFAULT_WIDTH, DEFAULT_HEIGHT, DEFAULT_FPS);

    // Main render loop.
    while (!WindowShouldClose()) {
        BeginDrawing();

        // Drain the command queue and execute each entry via raylib.
        CmdEntry e;
        while (cmdq_pop(&g_queue, &e)) {
            commands_execute(e.parsed, e.conn_fd);
            protocol_free(e.parsed);
        }

        // Advance any active music streams.
        commands_update_music_streams();

        EndDrawing();
    }

    // Tear down.
    RLS_INFO("shutting down");
    server_stop(&g_server);
    cmdq_destroy(&g_queue);
    CloseWindow();

    return 0;
}
