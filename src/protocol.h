#pragma once
#include <cJSON.h>
#include <stdbool.h>
#include <stddef.h>

// ---------------------------------------------------------------------------
// Parsed command
// ---------------------------------------------------------------------------

typedef struct ParsedCmd {
    char  *id;   // NULL if absent; heap-allocated
    char  *cmd;  // command name; heap-allocated
    cJSON *args; // may be NULL; owned by this struct
} ParsedCmd;

// Parse one newline-terminated JSON line into a ParsedCmd.
// Returns NULL if the line is not valid JSON, is missing "cmd", or "cmd"
// is not a string.  Caller must call protocol_free() when done.
ParsedCmd *protocol_parse_line(const char *line);

// Free a ParsedCmd and all its fields.
void protocol_free(ParsedCmd *p);

// ---------------------------------------------------------------------------
// Response helpers — return heap-allocated strings; caller must free()
// ---------------------------------------------------------------------------

// {"id":"<id>","ok":true[,"result":<result>]}
// id and result may be NULL (id omitted, result omitted).
char *protocol_ok(const char *id, cJSON *result);

// {"id":"<id>","ok":false,"error":"<msg>"}
// id may be NULL.
char *protocol_error(const char *id, const char *msg);

// Send a response over a socket fd.  Appends "\n".
// Returns false on write error.
bool protocol_send(int fd, const char *json);

// ---------------------------------------------------------------------------
// JSON type parsers (no raylib dependency — usable from unit tests)
// ---------------------------------------------------------------------------

// Parse a two-element numeric JSON array [x, y] into floats.
// Returns false if val is NULL, not an array, not length 2, or contains
// non-numeric elements.
bool proto_parse_vec2(const cJSON *val, float *x, float *y);

// Parse a four-element numeric JSON array [x, y, width, height] into floats.
// Returns false on any structural or type mismatch.
bool proto_parse_rect(const cJSON *val, float *x, float *y, float *w, float *h);

// ---------------------------------------------------------------------------
// Line buffer — accumulates partial TCP reads and emits complete lines
// ---------------------------------------------------------------------------

#define LINEBUF_MAX (1 << 17)  // 128 KiB per connection

typedef struct {
    char   data[LINEBUF_MAX];
    size_t len;
} LineBuffer;

void linebuf_init(LineBuffer *lb);

// Feed n bytes of received data into the buffer.
// For each complete '\n'-terminated line found, calls cb(line, userdata)
// with a NUL-terminated string (newline stripped).
// Returns -1 if a line exceeds LINEBUF_MAX (the connection should be dropped).
int linebuf_feed(LineBuffer *lb, const char *buf, size_t n,
                 void (*cb)(const char *line, void *userdata), void *userdata);

// ---------------------------------------------------------------------------
// Sync detection
// ---------------------------------------------------------------------------

// Returns true for commands that must be executed synchronously on the main
// thread before a response can be sent (e.g. Load*, Upload*, Get*, Is*,
// Measure*).  The connection thread must NOT send an optimistic ACK for
// these commands — the real response comes from commands_execute().
bool protocol_needs_response(const char *cmd);
