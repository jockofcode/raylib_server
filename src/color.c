#include "color.h"
#include <string.h>

typedef struct { const char *name; uint8_t r, g, b, a; } NamedColor;

// Values match raylib's predefined colors.
static const NamedColor NAMED_COLORS[] = {
    {"LIGHTGRAY",  200, 200, 200, 255},
    {"GRAY",       130, 130, 130, 255},
    {"DARKGRAY",    80,  80,  80, 255},
    {"YELLOW",     253, 249,   0, 255},
    {"GOLD",       255, 203,   0, 255},
    {"ORANGE",     255, 161,   0, 255},
    {"PINK",       255, 109, 194, 255},
    {"RED",        230,  41,  55, 255},
    {"MAROON",     190,  33,  55, 255},
    {"GREEN",        0, 228,  48, 255},
    {"LIME",         0, 158,  47, 255},
    {"DARKGREEN",    0, 117,  44, 255},
    {"SKYBLUE",    102, 191, 255, 255},
    {"BLUE",         0, 121, 241, 255},
    {"DARKBLUE",     0,  82, 172, 255},
    {"PURPLE",     200, 122, 255, 255},
    {"VIOLET",     135,  60, 190, 255},
    {"DARKPURPLE", 112,  31, 126, 255},
    {"BEIGE",      211, 176, 131, 255},
    {"BROWN",      127, 106,  79, 255},
    {"DARKBROWN",   76,  63,  47, 255},
    {"WHITE",      255, 255, 255, 255},
    {"BLACK",        0,   0,   0, 255},
    {"BLANK",        0,   0,   0,   0},
    {"MAGENTA",    255,   0, 255, 255},
    {"CYAN",         0, 255, 255, 255},
    {"RAYWHITE",   245, 245, 245, 255},
    {NULL,           0,   0,   0,   0},
};

bool color_named(const char *name,
                 uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a) {
    for (int i = 0; NAMED_COLORS[i].name; i++) {
        if (strcmp(name, NAMED_COLORS[i].name) == 0) {
            *r = NAMED_COLORS[i].r;
            *g = NAMED_COLORS[i].g;
            *b = NAMED_COLORS[i].b;
            *a = NAMED_COLORS[i].a;
            return true;
        }
    }
    return false;
}

bool color_from_json(const cJSON *val,
                     uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a) {
    if (!val) return false;

    if (cJSON_IsString(val)) {
        return color_named(val->valuestring, r, g, b, a);
    }

    if (cJSON_IsArray(val)) {
        int len = cJSON_GetArraySize(val);
        if (len < 3 || len > 4) return false;

        cJSON *item;
        int idx = 0;
        uint8_t components[4] = {0, 0, 0, 255};
        cJSON_ArrayForEach(item, val) {
            if (!cJSON_IsNumber(item)) return false;
            int v = (int)item->valuedouble;
            if (v < 0 || v > 255) return false;
            components[idx++] = (uint8_t)v;
        }
        *r = components[0];
        *g = components[1];
        *b = components[2];
        *a = components[3];
        return true;
    }

    return false;
}
