#pragma once
#include "protocol.h"
#include <stdbool.h>
#include <pthread.h>

#define DL_MAX_LISTS  64
#define DL_MAX_CMDS   4096
#define DL_NAME_MAX   256

typedef struct {
    char        name[DL_NAME_MAX];
    ParsedCmd **cmds;
    int         count;
    int         capacity;
} DisplayList;

typedef struct {
    pthread_mutex_t lock;
    DisplayList     lists[DL_MAX_LISTS];
    int             list_count;
    int             order[DL_MAX_LISTS];  /* list indices in render order */
    int             order_count;
    int             recording_idx;        /* -1 = not recording */
} DisplayListRegistry;

/* Initialise / destroy the registry. */
void dl_init(DisplayListRegistry *reg);
void dl_destroy(DisplayListRegistry *reg);

/*
 * Inspect and handle a client command.
 * Returns true if the command was consumed (caller must NOT push to queue).
 * Sends a wire response on conn_fd when cmd->id is set.
 *
 * Handles: DisplayListBegin/End/Clear/Delete/SetOrder,
 *          GetDisplayLists, GetDisplayListCommands,
 *          and any recordable draw command while recording is active.
 */
bool dl_handle_cmd(DisplayListRegistry *reg, const ParsedCmd *cmd, int conn_fd);

/*
 * Replay all recorded lists in render order.
 * For each recorded command, calls fn(cmd, userdata).
 * Holds the registry lock for the duration.
 */
void dl_replay(DisplayListRegistry *reg,
               void (*fn)(const ParsedCmd *, void *), void *userdata);

/* Query helpers — acquire lock before calling in a multi-threaded context. */
bool              dl_is_recording(const DisplayListRegistry *reg);
const DisplayList *dl_get(const DisplayListRegistry *reg, const char *name);
int               dl_count(const DisplayListRegistry *reg);
