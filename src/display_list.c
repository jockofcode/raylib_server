#include "display_list.h"
#include "protocol.h"
#include "rls_log.h"
#include <cJSON.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// Internal helpers (all call with lock held unless noted)
// ---------------------------------------------------------------------------

static int find_idx(const DisplayListRegistry *reg, const char *name) {
    for (int i = 0; i < reg->list_count; i++)
        if (strcmp(reg->lists[i].name, name) == 0) return i;
    return -1;
}

/* Find or create a list by name; returns index, or -1 if registry is full. */
static int alloc_idx(DisplayListRegistry *reg, const char *name) {
    int idx = find_idx(reg, name);
    if (idx >= 0) return idx;
    if (reg->list_count >= DL_MAX_LISTS) return -1;
    idx = reg->list_count++;
    strncpy(reg->lists[idx].name, name, DL_NAME_MAX - 1);
    reg->lists[idx].name[DL_NAME_MAX - 1] = '\0';
    reg->lists[idx].count    = 0;
    reg->lists[idx].capacity = 0;
    reg->lists[idx].cmds     = NULL;
    if (reg->order_count < DL_MAX_LISTS)
        reg->order[reg->order_count++] = idx;
    return idx;
}

static void free_list_cmds(DisplayList *list) {
    for (int i = 0; i < list->count; i++)
        protocol_free(list->cmds[i]);
    list->count = 0;
}

static void remove_from_order(DisplayListRegistry *reg, int list_idx) {
    for (int i = 0; i < reg->order_count; i++) {
        if (reg->order[i] == list_idx) {
            memmove(&reg->order[i], &reg->order[i + 1],
                    (size_t)(reg->order_count - i - 1) * sizeof(int));
            reg->order_count--;
            return;
        }
    }
}

/* Returns true if the command name should be stored in a display list. */
static bool is_recordable(const char *name) {
    if (strncmp(name, "Draw", 4) == 0)   return true;
    if (strcmp(name, "ClearBackground")  == 0) return true;
    if (strcmp(name, "BeginMode2D")      == 0) return true;
    if (strcmp(name, "EndMode2D")        == 0) return true;
    if (strcmp(name, "BeginMode3D")      == 0) return true;
    if (strcmp(name, "EndMode3D")        == 0) return true;
    if (strcmp(name, "BeginTextureMode") == 0) return true;
    if (strcmp(name, "EndTextureMode")   == 0) return true;
    if (strcmp(name, "BeginBlendMode")   == 0) return true;
    if (strcmp(name, "EndBlendMode")     == 0) return true;
    if (strcmp(name, "BeginScissorMode") == 0) return true;
    if (strcmp(name, "EndScissorMode")   == 0) return true;
    if (strcmp(name, "BeginShaderMode")  == 0) return true;
    if (strcmp(name, "EndShaderMode")    == 0) return true;
    return false;
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

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void dl_init(DisplayListRegistry *reg) {
    memset(reg, 0, sizeof(*reg));
    reg->recording_idx = -1;
    pthread_mutex_init(&reg->lock, NULL);
}

void dl_destroy(DisplayListRegistry *reg) {
    pthread_mutex_lock(&reg->lock);
    for (int i = 0; i < reg->list_count; i++) {
        free_list_cmds(&reg->lists[i]);
        free(reg->lists[i].cmds);
        reg->lists[i].cmds     = NULL;
        reg->lists[i].capacity = 0;
    }
    reg->list_count    = 0;
    reg->order_count   = 0;
    reg->recording_idx = -1;
    pthread_mutex_unlock(&reg->lock);
    pthread_mutex_destroy(&reg->lock);
}

bool dl_is_recording(const DisplayListRegistry *reg) {
    return reg->recording_idx >= 0;
}

const DisplayList *dl_get(const DisplayListRegistry *reg, const char *name) {
    int idx = find_idx(reg, name);
    return idx >= 0 ? &reg->lists[idx] : NULL;
}

int dl_count(const DisplayListRegistry *reg) {
    return reg->list_count;
}

// ---------------------------------------------------------------------------
// dl_handle_cmd
// ---------------------------------------------------------------------------

bool dl_handle_cmd(DisplayListRegistry *reg, const ParsedCmd *cmd, int conn_fd) {
    const char *name = cmd->cmd;

    // -------------------------------------------------------------------
    // DisplayListBegin
    // -------------------------------------------------------------------
    if (strcmp(name, "DisplayListBegin") == 0) {
        const char *list_name = NULL;
        if (cmd->args) {
            cJSON *n = cJSON_GetObjectItemCaseSensitive(cmd->args, "name");
            if (cJSON_IsString(n)) list_name = n->valuestring;
        }
        if (!list_name || list_name[0] == '\0') {
            send_err(conn_fd, cmd->id, "missing name");
            return true;
        }
        pthread_mutex_lock(&reg->lock);
        int idx = alloc_idx(reg, list_name);
        if (idx < 0) {
            pthread_mutex_unlock(&reg->lock);
            send_err(conn_fd, cmd->id, "display list registry full");
            return true;
        }
        free_list_cmds(&reg->lists[idx]);
        reg->recording_idx = idx;
        pthread_mutex_unlock(&reg->lock);
        send_ok(conn_fd, cmd->id);
        RLS_DEBUG("display list: begin recording '%s'", list_name);
        return true;
    }

    // -------------------------------------------------------------------
    // DisplayListEnd
    // -------------------------------------------------------------------
    if (strcmp(name, "DisplayListEnd") == 0) {
        pthread_mutex_lock(&reg->lock);
        reg->recording_idx = -1;
        pthread_mutex_unlock(&reg->lock);
        send_ok(conn_fd, cmd->id);
        RLS_DEBUG("display list: end recording");
        return true;
    }

    // -------------------------------------------------------------------
    // DisplayListClear
    // -------------------------------------------------------------------
    if (strcmp(name, "DisplayListClear") == 0) {
        const char *list_name = NULL;
        if (cmd->args) {
            cJSON *n = cJSON_GetObjectItemCaseSensitive(cmd->args, "name");
            if (cJSON_IsString(n)) list_name = n->valuestring;
        }
        if (!list_name) { send_err(conn_fd, cmd->id, "missing name"); return true; }
        pthread_mutex_lock(&reg->lock);
        int idx = find_idx(reg, list_name);
        if (idx >= 0) free_list_cmds(&reg->lists[idx]);
        pthread_mutex_unlock(&reg->lock);
        send_ok(conn_fd, cmd->id);
        return true;
    }

    // -------------------------------------------------------------------
    // DisplayListDelete
    // -------------------------------------------------------------------
    if (strcmp(name, "DisplayListDelete") == 0) {
        const char *list_name = NULL;
        if (cmd->args) {
            cJSON *n = cJSON_GetObjectItemCaseSensitive(cmd->args, "name");
            if (cJSON_IsString(n)) list_name = n->valuestring;
        }
        if (!list_name) { send_err(conn_fd, cmd->id, "missing name"); return true; }
        pthread_mutex_lock(&reg->lock);
        int idx = find_idx(reg, list_name);
        if (idx >= 0) {
            // Adjust recording index.
            if (reg->recording_idx == idx)       reg->recording_idx = -1;
            else if (reg->recording_idx > idx)   reg->recording_idx--;
            // Free commands and storage.
            free_list_cmds(&reg->lists[idx]);
            free(reg->lists[idx].cmds);
            reg->lists[idx].cmds     = NULL;
            reg->lists[idx].capacity = 0;
            // Remove from order and fix remaining indices.
            remove_from_order(reg, idx);
            for (int i = 0; i < reg->order_count; i++)
                if (reg->order[i] > idx) reg->order[i]--;
            // Compact lists array.
            memmove(&reg->lists[idx], &reg->lists[idx + 1],
                    (size_t)(reg->list_count - idx - 1) * sizeof(DisplayList));
            reg->list_count--;
        }
        pthread_mutex_unlock(&reg->lock);
        send_ok(conn_fd, cmd->id);
        return true;
    }

    // -------------------------------------------------------------------
    // DisplayListSetOrder
    // -------------------------------------------------------------------
    if (strcmp(name, "DisplayListSetOrder") == 0) {
        if (!cmd->args) { send_err(conn_fd, cmd->id, "missing names"); return true; }
        cJSON *names_arr = cJSON_GetObjectItemCaseSensitive(cmd->args, "names");
        if (!cJSON_IsArray(names_arr)) {
            send_err(conn_fd, cmd->id, "names must be an array");
            return true;
        }
        pthread_mutex_lock(&reg->lock);
        int  new_order[DL_MAX_LISTS];
        int  new_count = 0;
        bool seen[DL_MAX_LISTS];
        memset(seen, 0, sizeof(seen));
        cJSON *item;
        cJSON_ArrayForEach(item, names_arr) {
            if (!cJSON_IsString(item)) continue;
            int li = find_idx(reg, item->valuestring);
            if (li >= 0 && !seen[li] && new_count < DL_MAX_LISTS) {
                new_order[new_count++] = li;
                seen[li] = true;
            }
        }
        // Append lists not mentioned, preserving their existing relative order.
        for (int oi = 0; oi < reg->order_count; oi++) {
            int li = reg->order[oi];
            if (!seen[li] && new_count < DL_MAX_LISTS) {
                new_order[new_count++] = li;
                seen[li] = true;
            }
        }
        memcpy(reg->order, new_order, (size_t)new_count * sizeof(int));
        reg->order_count = new_count;
        pthread_mutex_unlock(&reg->lock);
        send_ok(conn_fd, cmd->id);
        return true;
    }

    // -------------------------------------------------------------------
    // GetDisplayLists
    // -------------------------------------------------------------------
    if (strcmp(name, "GetDisplayLists") == 0) {
        pthread_mutex_lock(&reg->lock);
        cJSON *arr = cJSON_CreateArray();
        for (int oi = 0; oi < reg->order_count; oi++) {
            int li = reg->order[oi];
            cJSON *entry = cJSON_CreateObject();
            cJSON_AddStringToObject(entry, "name",         reg->lists[li].name);
            cJSON_AddNumberToObject(entry, "commandCount", reg->lists[li].count);
            cJSON_AddItemToArray(arr, entry);
        }
        pthread_mutex_unlock(&reg->lock);
        cJSON *result = cJSON_CreateObject();
        cJSON_AddItemToObject(result, "lists", arr);
        if (cmd->id) {
            char *resp = protocol_ok(cmd->id, result);
            protocol_send(conn_fd, resp);
            free(resp);
        }
        cJSON_Delete(result);
        return true;
    }

    // -------------------------------------------------------------------
    // GetDisplayListCommands
    // -------------------------------------------------------------------
    if (strcmp(name, "GetDisplayListCommands") == 0) {
        const char *list_name = NULL;
        if (cmd->args) {
            cJSON *n = cJSON_GetObjectItemCaseSensitive(cmd->args, "name");
            if (cJSON_IsString(n)) list_name = n->valuestring;
        }
        if (!list_name) { send_err(conn_fd, cmd->id, "missing name"); return true; }
        pthread_mutex_lock(&reg->lock);
        int idx = find_idx(reg, list_name);
        if (idx < 0) {
            pthread_mutex_unlock(&reg->lock);
            send_err(conn_fd, cmd->id, "list not found");
            return true;
        }
        cJSON *arr = cJSON_CreateArray();
        for (int i = 0; i < reg->lists[idx].count; i++) {
            const ParsedCmd *c = reg->lists[idx].cmds[i];
            cJSON *entry = cJSON_CreateObject();
            cJSON_AddStringToObject(entry, "cmd", c->cmd);
            if (c->args)
                cJSON_AddItemToObject(entry, "args", cJSON_Duplicate(c->args, 1));
            cJSON_AddItemToArray(arr, entry);
        }
        pthread_mutex_unlock(&reg->lock);
        cJSON *result = cJSON_CreateObject();
        cJSON_AddItemToObject(result, "commands", arr);
        if (cmd->id) {
            char *resp = protocol_ok(cmd->id, result);
            protocol_send(conn_fd, resp);
            free(resp);
        }
        cJSON_Delete(result);
        return true;
    }

    // -------------------------------------------------------------------
    // Recording intercept — capture draw commands into the active list
    // -------------------------------------------------------------------
    pthread_mutex_lock(&reg->lock);
    bool recorded = false;
    if (reg->recording_idx >= 0 && is_recordable(name)) {
        ParsedCmd *copy = protocol_clone(cmd);
        if (copy) {
            DisplayList *list = &reg->lists[reg->recording_idx];
            if (list->count < DL_MAX_CMDS) {
                if (list->count >= list->capacity) {
                    int new_cap = list->capacity == 0 ? 16 : list->capacity * 2;
                    if (new_cap > DL_MAX_CMDS) new_cap = DL_MAX_CMDS;
                    ParsedCmd **tmp = realloc(list->cmds,
                                             (size_t)new_cap * sizeof(ParsedCmd *));
                    if (tmp) {
                        list->cmds     = tmp;
                        list->capacity = new_cap;
                    } else {
                        protocol_free(copy);
                        copy = NULL;
                    }
                }
                if (copy) {
                    list->cmds[list->count++] = copy;
                    recorded = true;
                }
            } else {
                protocol_free(copy);
                RLS_WARNING("display list full, command dropped");
            }
        }
    }
    pthread_mutex_unlock(&reg->lock);

    if (recorded) {
        send_ok(conn_fd, cmd->id);
        return true;
    }

    return false;
}

// ---------------------------------------------------------------------------
// dl_replay
// ---------------------------------------------------------------------------

void dl_replay(DisplayListRegistry *reg,
               void (*fn)(const ParsedCmd *, void *), void *userdata) {
    pthread_mutex_lock(&reg->lock);
    for (int oi = 0; oi < reg->order_count; oi++) {
        int li = reg->order[oi];
        for (int ci = 0; ci < reg->lists[li].count; ci++)
            fn(reg->lists[li].cmds[ci], userdata);
    }
    pthread_mutex_unlock(&reg->lock);
}
