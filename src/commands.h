#pragma once
#include "protocol.h"
#include "handle_registry.h"

// Initialise the command module with the resource registry.
// Must be called before the first commands_execute call.
void commands_init(HandleRegistry *reg);

// Execute a single parsed command using raylib.
// Must be called from the main thread (raylib/OpenGL requirement).
//
// For fire-and-forget commands, responses have already been sent
// optimistically by the connection thread.  For sync commands (Load*,
// Upload*, Measure*, etc.) the response is sent here via conn_fd.
void commands_execute(const ParsedCmd *cmd, int conn_fd);

// Call each frame from the main render loop so active music streams advance.
void commands_update_music_streams(void);
