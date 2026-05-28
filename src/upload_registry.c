#include "upload_registry.h"
#include "protocol.h"
#include "b64.h"
#include "rls_log.h"
#include <cJSON.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static PendingUpload *find_slot(UploadRegistry *reg, const char *upload_id) {
    for (int i = 0; i < UR_MAX_SLOTS; i++)
        if (reg->slots[i].active &&
            strcmp(reg->slots[i].upload_id, upload_id) == 0)
            return &reg->slots[i];
    return NULL;
}

static PendingUpload *alloc_slot(UploadRegistry *reg) {
    for (int i = 0; i < UR_MAX_SLOTS; i++)
        if (!reg->slots[i].active) return &reg->slots[i];
    return NULL;
}

static void free_slot(PendingUpload *slot) {
    free(slot->buf);
    memset(slot, 0, sizeof(*slot));
}

static void send_ok(int fd, const char *id) {
    if (!id || fd < 0) return;
    char *resp = protocol_ok(id, NULL);
    protocol_send(fd, resp);
    free(resp);
}

static void send_err(int fd, const char *id, const char *msg) {
    if (!id || fd < 0) return;
    char *resp = protocol_error(id, msg);
    protocol_send(fd, resp);
    free(resp);
}

static const char *get_str(cJSON *obj, const char *key) {
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return (cJSON_IsString(item) && item->valuestring) ? item->valuestring : NULL;
}

static long long get_ll(cJSON *obj, const char *key, long long fallback) {
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsNumber(item) ? (long long)item->valuedouble : fallback;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void ur_init(UploadRegistry *reg) {
    memset(reg, 0, sizeof(*reg));
    pthread_mutex_init(&reg->lock, NULL);
}

void ur_destroy(UploadRegistry *reg) {
    pthread_mutex_lock(&reg->lock);
    for (int i = 0; i < UR_MAX_SLOTS; i++)
        if (reg->slots[i].active) free_slot(&reg->slots[i]);
    pthread_mutex_unlock(&reg->lock);
    pthread_mutex_destroy(&reg->lock);
}

bool ur_handle_cmd(UploadRegistry *reg, const ParsedCmd *cmd, int conn_fd) {
    const char *name = cmd->cmd;
    cJSON      *args = cmd->args;

    // -------------------------------------------------------------------
    // BeginUpload
    // -------------------------------------------------------------------
    if (strcmp(name, "BeginUpload") == 0) {
        const char *uname = get_str(args, "name");
        const char *ftype = get_str(args, "fileType");
        long long   total = get_ll(args, "totalBytes", -1);

        if (!uname || !ftype || total <= 0) {
            send_err(conn_fd, cmd->id, "missing name, fileType, or totalBytes");
            return true;
        }
        if ((size_t)total > UR_MAX_BYTES) {
            send_err(conn_fd, cmd->id, "totalBytes exceeds 64 MB limit");
            return true;
        }

        pthread_mutex_lock(&reg->lock);
        PendingUpload *slot = alloc_slot(reg);
        if (!slot) {
            pthread_mutex_unlock(&reg->lock);
            send_err(conn_fd, cmd->id, "upload registry full");
            return true;
        }

        unsigned char *buf = malloc((size_t)total);
        if (!buf) {
            pthread_mutex_unlock(&reg->lock);
            send_err(conn_fd, cmd->id, "out of memory");
            return true;
        }

        int id_num = reg->next_id++;
        snprintf(slot->upload_id, UR_UPLOAD_ID_LEN, "u-%d", id_num);
        strncpy(slot->name,      uname, UR_NAME_LEN      - 1);
        strncpy(slot->file_type, ftype, UR_FILE_TYPE_LEN - 1);
        slot->buf            = buf;
        slot->total_bytes    = (size_t)total;
        slot->bytes_received = 0;
        slot->next_seq       = 0;
        slot->active         = true;

        char upload_id_copy[UR_UPLOAD_ID_LEN];
        strncpy(upload_id_copy, slot->upload_id, UR_UPLOAD_ID_LEN);
        pthread_mutex_unlock(&reg->lock);

        RLS_DEBUG("BeginUpload: id=%s name=%s type=%s total=%lld",
                  upload_id_copy, uname, ftype, total);

        if (cmd->id) {
            cJSON *result = cJSON_CreateObject();
            cJSON_AddStringToObject(result, "uploadId", upload_id_copy);
            char *resp = protocol_ok(cmd->id, result);
            cJSON_Delete(result);
            protocol_send(conn_fd, resp);
            free(resp);
        }
        return true;
    }

    // -------------------------------------------------------------------
    // UploadChunk
    // -------------------------------------------------------------------
    if (strcmp(name, "UploadChunk") == 0) {
        const char *upload_id = get_str(args, "uploadId");
        const char *b64_data  = get_str(args, "data");
        long long   seq       = get_ll(args, "seq", -1);

        if (!upload_id || !b64_data || seq < 0) {
            send_err(conn_fd, cmd->id, "missing uploadId, seq, or data");
            return true;
        }

        pthread_mutex_lock(&reg->lock);
        PendingUpload *slot = find_slot(reg, upload_id);
        if (!slot) {
            pthread_mutex_unlock(&reg->lock);
            send_err(conn_fd, cmd->id, "upload not found");
            return true;
        }
        if ((int)seq != slot->next_seq) {
            int expected = slot->next_seq;
            pthread_mutex_unlock(&reg->lock);
            char msg[64];
            snprintf(msg, sizeof(msg), "out-of-order chunk: got %lld expected %d",
                     seq, expected);
            send_err(conn_fd, cmd->id, msg);
            return true;
        }

        size_t decoded_len = 0;
        unsigned char *decoded = b64_decode(b64_data, strlen(b64_data), &decoded_len);
        if (!decoded) {
            pthread_mutex_unlock(&reg->lock);
            send_err(conn_fd, cmd->id, "base64 decode failed");
            return true;
        }
        if (slot->bytes_received + decoded_len > slot->total_bytes) {
            free(decoded);
            pthread_mutex_unlock(&reg->lock);
            send_err(conn_fd, cmd->id, "chunk exceeds declared totalBytes");
            return true;
        }

        memcpy(slot->buf + slot->bytes_received, decoded, decoded_len);
        slot->bytes_received += decoded_len;
        slot->next_seq++;
        free(decoded);
        pthread_mutex_unlock(&reg->lock);

        send_ok(conn_fd, cmd->id);
        return true;
    }

    // -------------------------------------------------------------------
    // AbortUpload
    // -------------------------------------------------------------------
    if (strcmp(name, "AbortUpload") == 0) {
        const char *upload_id = get_str(args, "uploadId");
        if (!upload_id) { send_err(conn_fd, cmd->id, "missing uploadId"); return true; }

        pthread_mutex_lock(&reg->lock);
        PendingUpload *slot = find_slot(reg, upload_id);
        if (slot) free_slot(slot);
        pthread_mutex_unlock(&reg->lock);

        send_ok(conn_fd, cmd->id);
        return true;
    }

    // -------------------------------------------------------------------
    // ListUploads
    // -------------------------------------------------------------------
    if (strcmp(name, "ListUploads") == 0) {
        pthread_mutex_lock(&reg->lock);
        cJSON *arr = cJSON_CreateArray();
        for (int i = 0; i < UR_MAX_SLOTS; i++) {
            if (!reg->slots[i].active) continue;
            cJSON *entry = cJSON_CreateObject();
            cJSON_AddStringToObject(entry, "uploadId",      reg->slots[i].upload_id);
            cJSON_AddStringToObject(entry, "name",          reg->slots[i].name);
            cJSON_AddNumberToObject(entry, "bytesReceived", (double)reg->slots[i].bytes_received);
            cJSON_AddNumberToObject(entry, "totalBytes",    (double)reg->slots[i].total_bytes);
            cJSON_AddItemToArray(arr, entry);
        }
        pthread_mutex_unlock(&reg->lock);

        cJSON *result = cJSON_CreateObject();
        cJSON_AddItemToObject(result, "uploads", arr);
        if (cmd->id) {
            char *resp = protocol_ok(cmd->id, result);
            protocol_send(conn_fd, resp);
            free(resp);
        }
        cJSON_Delete(result);
        return true;
    }

    return false;
}

bool ur_commit_take(UploadRegistry *reg, const char *upload_id,
                    UploadCommitInfo *out) {
    pthread_mutex_lock(&reg->lock);
    PendingUpload *slot = find_slot(reg, upload_id);
    if (!slot) {
        pthread_mutex_unlock(&reg->lock);
        return false;
    }
    if (slot->bytes_received < slot->total_bytes) {
        pthread_mutex_unlock(&reg->lock);
        return false;
    }

    strncpy(out->file_type,  slot->file_type,  UR_FILE_TYPE_LEN - 1);
    out->file_type[UR_FILE_TYPE_LEN - 1] = '\0';
    out->buf         = slot->buf;
    out->total_bytes = slot->total_bytes;

    // Clear slot without freeing buf — caller owns it now.
    slot->buf    = NULL;
    slot->active = false;
    memset(slot, 0, sizeof(*slot));

    pthread_mutex_unlock(&reg->lock);
    return true;
}
