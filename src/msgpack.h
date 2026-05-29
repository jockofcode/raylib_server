#pragma once
#include <cJSON.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Minimal MessagePack encoder / decoder.
//
// Only the subset needed by the raylib_server wire protocol is implemented:
//   Maps (fixmap / map16 / map32)
//   Arrays (fixarray / array16 / array32)
//   Strings (fixstr / str8 / str16)
//   Integers (fixint / int8 / int16 / int32 / int64 /
//              uint8 / uint16 / uint32 / uint64)
//   Floats (float64 / float32)
//   Booleans (true / false)
//   Nil
//
// All values are exchanged as cJSON objects so the rest of the server pipeline
// (protocol_parse_line → commands_execute) is unchanged.
// ---------------------------------------------------------------------------

// Decode a MessagePack buffer into a cJSON value.
// Returns NULL on parse error or unsupported type.
// Caller must cJSON_Delete() the result.
cJSON *mp_decode(const uint8_t *buf, size_t len);

// Encode a cJSON value into MessagePack.
// On success, sets *out to a malloc'd buffer of *out_len bytes and returns true.
// Caller must free() *out.
// Returns false on error (OOM, unsupported type).
bool mp_encode(const cJSON *val, uint8_t **out, size_t *out_len);
