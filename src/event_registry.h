#pragma once
#include "protocol.h"
#include <stdbool.h>
#include <pthread.h>

// ---------------------------------------------------------------------------
// Event kinds
// ---------------------------------------------------------------------------

typedef enum {
    EVENT_KEY_PRESSED = 0,
    EVENT_KEY_RELEASED,
    EVENT_MOUSE_MOVED,
    EVENT_MOUSE_BUTTON_PRESSED,
    EVENT_MOUSE_BUTTON_RELEASED,
    EVENT_MOUSE_WHEEL,
    EVENT_WINDOW_RESIZED,
    EVENT_WINDOW_FOCUSED,
    EVENT_WINDOW_UNFOCUSED,
    EVENT_WINDOW_CLOSED,
    EVENT_GESTURE_DETECTED,
    EVENT_TIMER_FIRED,
    EVENT_KIND_COUNT,
} EventKind;

typedef unsigned int EventMask;
#define EVENT_MASK(k)  (1u << (unsigned)(k))
#define EVENT_MASK_ALL ((1u << EVENT_KIND_COUNT) - 1u)

// ---------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------

#define ER_MAX_SUBS 256

typedef struct {
    int       fd;
    EventMask mask;
} EventSub;

typedef struct {
    pthread_mutex_t lock;
    EventSub        subs[ER_MAX_SUBS];
    int             count;
} EventRegistry;

void er_init(EventRegistry *reg);
void er_destroy(EventRegistry *reg);

// Add events to a connection's subscription. Creates an entry if needed.
void er_subscribe(EventRegistry *reg, int fd, EventMask mask);

// Remove events from a connection's subscription.
void er_unsubscribe(EventRegistry *reg, int fd, EventMask mask);

// Remove all subscriptions for a connection fd (call on disconnect).
void er_remove_fd(EventRegistry *reg, int fd);

// Push a JSON event to all connections subscribed to kind.
// Dead connections (write fails) are silently removed.
void er_push(EventRegistry *reg, EventKind kind, const char *json);

// Handle a Subscribe or Unsubscribe command from the connection thread.
// Returns true if the command was consumed.
// Sends an ACK or error on conn_fd when cmd->id is set.
bool er_handle_cmd(EventRegistry *reg, const ParsedCmd *cmd, int conn_fd);

// Convert an event name string to an EventKind, or -1 if unknown.
int er_kind_from_name(const char *name);
