#pragma once
#include <cJSON.h>
#include <stdint.h>
#include <stdbool.h>

// Parse a cJSON color value into RGBA bytes.
// Accepts:
//   - JSON string "RED", "BLUE", etc. (raylib named colors)
//   - JSON array  [r, g, b, a]  (integer values 0–255)
// Returns false if the value is not a recognised color.
bool color_from_json(const cJSON *val,
                     uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a);

// Look up a raylib named color string.  Returns false if not recognised.
bool color_named(const char *name,
                 uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a);
