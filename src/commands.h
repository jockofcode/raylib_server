#pragma once
#include "protocol.h"
#include "handle_registry.h"
#include "display_list.h"
#include "upload_registry.h"
#include "event_registry.h"

// Initialise the command module with the resource, display-list, upload, and event registries.
// Must be called before the first commands_execute call.
void commands_init(HandleRegistry *reg, DisplayListRegistry *dl_reg,
                   UploadRegistry *ur_reg, EventRegistry *ev_reg);

// Execute a single parsed command using raylib.
// Must be called from the main thread (raylib/OpenGL requirement).
//
// For fire-and-forget commands, responses have already been sent
// optimistically by the connection thread.  For sync commands (Load*,
// Upload*, Measure*, etc.) the response is sent here via conn_fd.
void commands_execute(const ParsedCmd *cmd, int conn_fd);

// Replay all display lists in their defined order.
// Must be called from the main thread inside BeginDrawing/EndDrawing.
void commands_replay_display_lists(void);

// Call each frame from the main render loop so active music streams advance.
void commands_update_music_streams(void);

// Set the TCP port number reported by GetServerInfo. Call after server_init.
void commands_set_port(int port);

// Increment the internal frame counter. Call once per frame from the render loop.
void commands_tick_frame(void);

// Detect input and window events this frame and push them to subscribed clients.
// Call once per frame from the render loop, after commands_tick_frame.
void commands_push_events(void);
