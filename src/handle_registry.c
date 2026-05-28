#include "handle_registry.h"
#include <string.h>

void handle_registry_init(HandleRegistry *r) {
    memset(r, 0, sizeof(*r));
    r->next_hint = 0;
}

int handle_alloc(HandleRegistry *r, HandleKind kind, void *ptr) {
    // Linear scan from next_hint for a free slot.
    for (int i = 0; i < HANDLE_MAX; i++) {
        int idx = (r->next_hint + i) % HANDLE_MAX;
        if (!r->slots[idx].used) {
            r->slots[idx].ptr     = ptr;
            r->slots[idx].kind    = kind;
            r->slots[idx].used    = true;
            r->next_hint = (idx + 1) % HANDLE_MAX;
            return idx + 1; // handles are 1-based
        }
    }
    return 0; // registry full
}

void *handle_get(HandleRegistry *r, int id, HandleKind kind) {
    if (id < 1 || id > HANDLE_MAX) return NULL;
    HandleSlot *s = &r->slots[id - 1];
    if (!s->used) return NULL;
    if (s->kind != kind) return NULL;
    return s->ptr;
}

bool handle_free(HandleRegistry *r, int id) {
    if (id < 1 || id > HANDLE_MAX) return false;
    HandleSlot *s = &r->slots[id - 1];
    if (!s->used) return false;
    s->used = false;
    s->ptr  = NULL;
    s->kind = HANDLE_NONE;
    // Hint at the freed slot for the next allocation.
    r->next_hint = id - 1;
    return true;
}

bool handle_valid(HandleRegistry *r, int id) {
    if (id < 1 || id > HANDLE_MAX) return false;
    return r->slots[id - 1].used;
}

HandleKind handle_kind(HandleRegistry *r, int id) {
    if (id < 1 || id > HANDLE_MAX) return HANDLE_NONE;
    if (!r->slots[id - 1].used) return HANDLE_NONE;
    return r->slots[id - 1].kind;
}

void handle_iterate(HandleRegistry *r, HandleKind kind,
                    void (*fn)(int id, void *ptr, void *userdata),
                    void *userdata) {
    for (int i = 0; i < HANDLE_MAX; i++) {
        if (r->slots[i].used && r->slots[i].kind == kind)
            fn(i + 1, r->slots[i].ptr, userdata);
    }
}
