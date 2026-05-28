#include "protocol.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// ---------------------------------------------------------------------------
// ParsedCmd
// ---------------------------------------------------------------------------

ParsedCmd *protocol_parse_line(const char *line) {
    cJSON *root = cJSON_Parse(line);
    if (!root) return NULL;

    cJSON *cmd_item = cJSON_GetObjectItemCaseSensitive(root, "cmd");
    if (!cJSON_IsString(cmd_item) || !cmd_item->valuestring) {
        cJSON_Delete(root);
        return NULL;
    }

    ParsedCmd *p = calloc(1, sizeof(ParsedCmd));
    if (!p) { cJSON_Delete(root); return NULL; }

    p->cmd = strdup(cmd_item->valuestring);

    cJSON *id_item = cJSON_GetObjectItemCaseSensitive(root, "id");
    if (cJSON_IsString(id_item) && id_item->valuestring) {
        p->id = strdup(id_item->valuestring);
    }

    cJSON *args_item = cJSON_GetObjectItemCaseSensitive(root, "args");
    if (args_item) {
        p->args = cJSON_DetachItemFromObject(root, "args");
    }

    cJSON_Delete(root);
    return p;
}

void protocol_free(ParsedCmd *p) {
    if (!p) return;
    free(p->id);
    free(p->cmd);
    cJSON_Delete(p->args);
    free(p);
}

// ---------------------------------------------------------------------------
// Response builders
// ---------------------------------------------------------------------------

char *protocol_ok(const char *id, cJSON *result) {
    cJSON *root = cJSON_CreateObject();
    if (id) cJSON_AddStringToObject(root, "id", id);
    cJSON_AddTrueToObject(root, "ok");
    if (result) {
        cJSON_AddItemToObject(root, "result", cJSON_Duplicate(result, true));
    }
    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return s;
}

char *protocol_error(const char *id, const char *msg) {
    cJSON *root = cJSON_CreateObject();
    if (id) cJSON_AddStringToObject(root, "id", id);
    cJSON_AddFalseToObject(root, "ok");
    cJSON_AddStringToObject(root, "error", msg ? msg : "unknown error");
    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return s;
}

bool protocol_send(int fd, const char *json) {
    if (fd < 0 || !json) return false;
    size_t len = strlen(json);
    if (write(fd, json, len) != (ssize_t)len) return false;
    if (write(fd, "\n", 1) != 1) return false;
    return true;
}

// ---------------------------------------------------------------------------
// JSON type parsers
// ---------------------------------------------------------------------------

bool proto_parse_vec2(const cJSON *val, float *x, float *y) {
    if (!cJSON_IsArray(val)) return false;
    if (cJSON_GetArraySize(val) != 2) return false;
    const cJSON *xi = cJSON_GetArrayItem(val, 0);
    const cJSON *yi = cJSON_GetArrayItem(val, 1);
    if (!cJSON_IsNumber(xi) || !cJSON_IsNumber(yi)) return false;
    *x = (float)xi->valuedouble;
    *y = (float)yi->valuedouble;
    return true;
}

bool proto_parse_rect(const cJSON *val, float *x, float *y, float *w, float *h) {
    if (!cJSON_IsArray(val)) return false;
    if (cJSON_GetArraySize(val) != 4) return false;
    const cJSON *items[4];
    for (int i = 0; i < 4; i++) {
        items[i] = cJSON_GetArrayItem(val, i);
        if (!cJSON_IsNumber(items[i])) return false;
    }
    *x = (float)items[0]->valuedouble;
    *y = (float)items[1]->valuedouble;
    *w = (float)items[2]->valuedouble;
    *h = (float)items[3]->valuedouble;
    return true;
}

// ---------------------------------------------------------------------------
// LineBuffer
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Sync detection
// ---------------------------------------------------------------------------

bool protocol_needs_response(const char *cmd) {
    if (!cmd) return false;
    // Commands that must be executed on the main thread before a response can
    // be sent.  Prefix check covers Load*, Upload*, Get*, Is* families; exact
    // matches cover the remainder.
    static const char *prefixes[] = { "Load", "Upload", "Get", "Is", NULL };
    static const char *exact[]    = {
        "BeginUpload", "CommitUpload", "AbortUpload", "ListUploads",
        "MeasureText", "MeasureTextEx", "ListHandles", "GetServerInfo",
        NULL
    };
    for (int i = 0; exact[i]; i++)
        if (strcmp(cmd, exact[i]) == 0) return true;
    for (int i = 0; prefixes[i]; i++) {
        size_t len = strlen(prefixes[i]);
        if (strncmp(cmd, prefixes[i], len) == 0) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------

void linebuf_init(LineBuffer *lb) {
    lb->len = 0;
}

int linebuf_feed(LineBuffer *lb, const char *buf, size_t n,
                 void (*cb)(const char *line, void *userdata), void *userdata) {
    for (size_t i = 0; i < n; i++) {
        char c = buf[i];

        if (c == '\n') {
            lb->data[lb->len] = '\0';
            cb(lb->data, userdata);
            lb->len = 0;
            continue;
        }

        if (lb->len + 1 >= LINEBUF_MAX) {
            lb->len = 0;
            return -1;
        }

        lb->data[lb->len++] = c;
    }
    return 0;
}
