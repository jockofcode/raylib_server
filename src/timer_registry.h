#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>

// ---------------------------------------------------------------------------
// TimerRegistry — server-side animation timers.
//
// Timers fire callbacks (pushed as events to subscribed clients) at a fixed
// interval (repeating) or once after a delay (one-shot).
//
// Wire protocol:
//   TimerCreate  { "name":"myTimer", "interval":0.5, "repeat":true  }  → { "timerId": "..." }
//   TimerOnce    { "name":"myTimer", "delay":1.0 }                     → { "timerId": "..." }
//   TimerDelete  { "timerId":"..." }
//   ListTimers                                                          → { "timers":[...] }
//
// Events pushed to subscribed clients:
//   { "event":"TimerFired", "timerId":"...", "name":"...", "frame":<n> }
// ---------------------------------------------------------------------------

#define TIMER_MAX 256
#define TIMER_ID_LEN 16  // "t" + 14 hex chars + NUL

typedef struct {
    char     id[TIMER_ID_LEN];   // unique timer id
    char     name[64];           // human-readable label
    double   interval;           // seconds between fires
    double   elapsed;            // seconds since last fire (or creation)
    bool     repeat;             // true = repeating, false = one-shot
    bool     used;
} TimerSlot;

typedef struct {
    TimerSlot     slots[TIMER_MAX];
    pthread_mutex_t mutex;
} TimerRegistry;

void timer_registry_init(TimerRegistry *r);
void timer_registry_destroy(TimerRegistry *r);

// Create a repeating timer. Returns the timer id string (stored in out_id, which
// must be at least TIMER_ID_LEN bytes). Returns false if the registry is full.
bool timer_create(TimerRegistry *r, const char *name, double interval, bool repeat,
                  char out_id[TIMER_ID_LEN]);

// Delete a timer by id. Returns false if not found.
bool timer_delete(TimerRegistry *r, const char *id);

// Advance all timers by dt seconds. Calls fired_cb(id, name, userdata) for each
// timer that fires. One-shot timers are removed automatically after firing.
void timer_tick(TimerRegistry *r, double dt,
                void (*fired_cb)(const char *id, const char *name, void *userdata),
                void *userdata);

// Build a cJSON array describing all active timers. Caller owns the result.
#include <cJSON.h>
cJSON *timer_list_json(TimerRegistry *r);

// ---------------------------------------------------------------------------
// Connection-thread command handler (like er_handle_cmd / ur_handle_cmd).
// Handles TimerCreate, TimerOnce, TimerDelete, ListTimers.
// Returns true and sends a response if the command was handled.
// ---------------------------------------------------------------------------
#include "protocol.h"
bool tr_handle_cmd(TimerRegistry *r, const ParsedCmd *p, int conn_fd);
