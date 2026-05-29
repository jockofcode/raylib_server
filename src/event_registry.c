#include "event_registry.h"
#include "protocol.h"
#include <string.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// Event name table
// ---------------------------------------------------------------------------

static const char *g_event_names[EVENT_KIND_COUNT] = {
    "KeyPressed",
    "KeyReleased",
    "MouseMoved",
    "MouseButtonPressed",
    "MouseButtonReleased",
    "MouseWheel",
    "WindowResized",
    "WindowFocused",
    "WindowUnfocused",
    "WindowClosed",
    "GestureDetected",
    "TimerFired",
};

int er_kind_from_name(const char *name) {
    if (!name) return -1;
    for (int i = 0; i < EVENT_KIND_COUNT; i++)
        if (strcmp(name, g_event_names[i]) == 0) return i;
    return -1;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void er_init(EventRegistry *reg) {
    memset(reg, 0, sizeof(*reg));
    pthread_mutex_init(&reg->lock, NULL);
}

void er_destroy(EventRegistry *reg) {
    pthread_mutex_destroy(&reg->lock);
}

// ---------------------------------------------------------------------------
// Subscription management
// ---------------------------------------------------------------------------

void er_subscribe(EventRegistry *reg, int fd, EventMask mask) {
    if (!reg || fd < 0 || !mask) return;
    pthread_mutex_lock(&reg->lock);
    for (int i = 0; i < reg->count; i++) {
        if (reg->subs[i].fd == fd) {
            reg->subs[i].mask |= mask;
            pthread_mutex_unlock(&reg->lock);
            return;
        }
    }
    if (reg->count < ER_MAX_SUBS)
        reg->subs[reg->count++] = (EventSub){ .fd = fd, .mask = mask };
    pthread_mutex_unlock(&reg->lock);
}

void er_unsubscribe(EventRegistry *reg, int fd, EventMask mask) {
    if (!reg || fd < 0) return;
    pthread_mutex_lock(&reg->lock);
    for (int i = 0; i < reg->count; i++) {
        if (reg->subs[i].fd == fd) {
            reg->subs[i].mask &= ~mask;
            if (!reg->subs[i].mask)
                reg->subs[i] = reg->subs[--reg->count];
            break;
        }
    }
    pthread_mutex_unlock(&reg->lock);
}

void er_remove_fd(EventRegistry *reg, int fd) {
    if (!reg || fd < 0) return;
    pthread_mutex_lock(&reg->lock);
    for (int i = 0; i < reg->count; i++) {
        if (reg->subs[i].fd == fd) {
            reg->subs[i] = reg->subs[--reg->count];
            break;
        }
    }
    pthread_mutex_unlock(&reg->lock);
}

// ---------------------------------------------------------------------------
// Event delivery
// ---------------------------------------------------------------------------

void er_push(EventRegistry *reg, EventKind kind, const char *json) {
    if (!reg || !json || (unsigned)kind >= (unsigned)EVENT_KIND_COUNT) return;
    EventMask bit = EVENT_MASK(kind);
    pthread_mutex_lock(&reg->lock);
    int i = 0;
    while (i < reg->count) {
        if (!(reg->subs[i].mask & bit)) { i++; continue; }
        if (!protocol_send(reg->subs[i].fd, json)) {
            // Write failed: dead connection; compact the list.
            reg->subs[i] = reg->subs[--reg->count];
            continue;
        }
        i++;
    }
    pthread_mutex_unlock(&reg->lock);
}

// ---------------------------------------------------------------------------
// Wire command handling (connection thread)
// ---------------------------------------------------------------------------

bool er_handle_cmd(EventRegistry *reg, const ParsedCmd *cmd, int conn_fd) {
    if (!cmd || !cmd->cmd) return false;
    bool is_sub   = strcmp(cmd->cmd, "Subscribe")   == 0;
    bool is_unsub = strcmp(cmd->cmd, "Unsubscribe") == 0;
    if (!is_sub && !is_unsub) return false;

    cJSON *events = cmd->args
        ? cJSON_GetObjectItemCaseSensitive(cmd->args, "events") : NULL;

    if (!cJSON_IsArray(events)) {
        if (cmd->id) {
            char *err = protocol_error(cmd->id, "args.events must be an array");
            protocol_send(conn_fd, err);
            free(err);
        }
        return true;
    }

    EventMask mask = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, events) {
        if (!cJSON_IsString(item)) continue;
        int k = er_kind_from_name(item->valuestring);
        if (k >= 0) mask |= EVENT_MASK(k);
        // Unknown event names are silently ignored.
    }

    if (is_sub)
        er_subscribe(reg, conn_fd, mask);
    else
        er_unsubscribe(reg, conn_fd, mask);

    if (cmd->id) {
        char *ack = protocol_ok(cmd->id, NULL);
        protocol_send(conn_fd, ack);
        free(ack);
    }
    return true;
}
