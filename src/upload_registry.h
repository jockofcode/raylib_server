#pragma once
#include "protocol.h"
#include <stdbool.h>
#include <stddef.h>
#include <pthread.h>

#define UR_MAX_SLOTS       32
#define UR_MAX_BYTES       (64 * 1024 * 1024)  /* 64 MB per upload */
#define UR_UPLOAD_ID_LEN   32
#define UR_NAME_LEN        256
#define UR_FILE_TYPE_LEN   32

typedef struct {
    char           upload_id[UR_UPLOAD_ID_LEN];
    char           name[UR_NAME_LEN];
    char           file_type[UR_FILE_TYPE_LEN];
    unsigned char *buf;
    size_t         total_bytes;
    size_t         bytes_received;
    int            next_seq;
    bool           active;
} PendingUpload;

typedef struct {
    pthread_mutex_t lock;
    PendingUpload   slots[UR_MAX_SLOTS];
    int             next_id;  /* monotonic counter for uploadId generation */
} UploadRegistry;

/* Initialise / destroy the registry. */
void ur_init(UploadRegistry *reg);
void ur_destroy(UploadRegistry *reg);

/*
 * Handle a client command in the connection thread.
 * Handles: BeginUpload, UploadChunk, AbortUpload, ListUploads.
 * Does NOT handle CommitUpload — that must go through the queue to the main
 * thread so raylib APIs (OpenGL, audio) can be called safely.
 * Returns true if the command was consumed.
 */
bool ur_handle_cmd(UploadRegistry *reg, const ParsedCmd *cmd, int conn_fd);

/*
 * Called from commands_execute (main thread) for CommitUpload.
 * Finds and removes the pending upload slot, handing ownership of the
 * assembled buffer to the caller.  The caller must free buf when done
 * (or retain it for streaming resources like font/music).
 * Returns false if the uploadId is not found or upload is incomplete.
 */
typedef struct {
    char           file_type[UR_FILE_TYPE_LEN];
    unsigned char *buf;          /* caller takes ownership */
    size_t         total_bytes;
} UploadCommitInfo;

bool ur_commit_take(UploadRegistry *reg, const char *upload_id,
                    UploadCommitInfo *out);
