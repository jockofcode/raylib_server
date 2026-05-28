#pragma once
#include <stdbool.h>
#include <stddef.h>

#define HANDLE_MAX 4096

typedef enum {
    HANDLE_NONE = 0,
    HANDLE_TEXTURE,
    HANDLE_FONT,
    HANDLE_RENDER_TEXTURE,
    HANDLE_SOUND,
    HANDLE_MUSIC,
    HANDLE_SHADER,
} HandleKind;

typedef struct {
    void      *ptr;
    HandleKind kind;
    bool       used;
} HandleSlot;

typedef struct {
    HandleSlot slots[HANDLE_MAX];
    int        next_hint; // start of next search (for O(1) amortised alloc)
} HandleRegistry;

void handle_registry_init(HandleRegistry *r);

// Allocate a handle.  Returns an id >= 1 on success, 0 if the registry is
// full.
int handle_alloc(HandleRegistry *r, HandleKind kind, void *ptr);

// Look up a handle by id and expected kind.  Returns the stored pointer, or
// NULL if the id is out of range, not in use, or the kind does not match.
void *handle_get(HandleRegistry *r, int id, HandleKind kind);

// Mark a handle as free.  Returns false if the id is invalid or not in use.
bool handle_free(HandleRegistry *r, int id);

// Returns true if the id is in range and currently in use.
bool handle_valid(HandleRegistry *r, int id);

// Returns the kind stored for an id, or HANDLE_NONE if the id is invalid.
HandleKind handle_kind(HandleRegistry *r, int id);

// Call fn(id, ptr, userdata) for every used slot whose kind matches.
void handle_iterate(HandleRegistry *r, HandleKind kind,
                    void (*fn)(int id, void *ptr, void *userdata),
                    void *userdata);
