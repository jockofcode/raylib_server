#pragma once
#include <stddef.h>

// Decode a base64-encoded string.  Returns a heap-allocated buffer; caller
// must free().  Sets *out_len to the number of decoded bytes.  Whitespace in
// the input is silently skipped.  Padding ('=') stops decoding.
// Returns NULL on allocation failure; never returns NULL for valid empty input.
unsigned char *b64_decode(const char *src, size_t src_len, size_t *out_len);
