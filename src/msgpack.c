#include "msgpack.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

// ---------------------------------------------------------------------------
// Decoder
// ---------------------------------------------------------------------------

typedef struct {
    const uint8_t *buf;
    size_t         len;
    size_t         pos;
} MpReader;

static bool mr_read(MpReader *r, uint8_t *out, size_t n) {
    if (r->pos + n > r->len) return false;
    memcpy(out, r->buf + r->pos, n);
    r->pos += n;
    return true;
}

static bool mr_byte(MpReader *r, uint8_t *out) {
    return mr_read(r, out, 1);
}

static bool mr_u16(MpReader *r, uint16_t *out) {
    uint8_t b[2];
    if (!mr_read(r, b, 2)) return false;
    *out = ((uint16_t)b[0] << 8) | b[1];
    return true;
}

static bool mr_u32(MpReader *r, uint32_t *out) {
    uint8_t b[4];
    if (!mr_read(r, b, 4)) return false;
    *out = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8)  | b[3];
    return true;
}

static bool mr_u64(MpReader *r, uint64_t *out) {
    uint8_t b[8];
    if (!mr_read(r, b, 8)) return false;
    *out = ((uint64_t)b[0] << 56) | ((uint64_t)b[1] << 48) |
           ((uint64_t)b[2] << 40) | ((uint64_t)b[3] << 32) |
           ((uint64_t)b[4] << 24) | ((uint64_t)b[5] << 16) |
           ((uint64_t)b[6] << 8)  | b[7];
    return true;
}

static cJSON *decode_value(MpReader *r);  // forward

static cJSON *decode_str(MpReader *r, size_t len) {
    if (r->pos + len > r->len) return NULL;
    char *s = malloc(len + 1);
    if (!s) return NULL;
    memcpy(s, r->buf + r->pos, len);
    s[len] = '\0';
    r->pos += len;
    cJSON *j = cJSON_CreateString(s);
    free(s);
    return j;
}

static cJSON *decode_map(MpReader *r, size_t count) {
    cJSON *obj = cJSON_CreateObject();
    if (!obj) return NULL;
    for (size_t i = 0; i < count; i++) {
        cJSON *key = decode_value(r);
        if (!key || !cJSON_IsString(key)) { cJSON_Delete(key); cJSON_Delete(obj); return NULL; }
        cJSON *val = decode_value(r);
        if (!val) { cJSON_Delete(key); cJSON_Delete(obj); return NULL; }
        cJSON_AddItemToObject(obj, key->valuestring, val);
        cJSON_Delete(key);
    }
    return obj;
}

static cJSON *decode_array(MpReader *r, size_t count) {
    cJSON *arr = cJSON_CreateArray();
    if (!arr) return NULL;
    for (size_t i = 0; i < count; i++) {
        cJSON *item = decode_value(r);
        if (!item) { cJSON_Delete(arr); return NULL; }
        cJSON_AddItemToArray(arr, item);
    }
    return arr;
}

static cJSON *decode_value(MpReader *r) {
    uint8_t b;
    if (!mr_byte(r, &b)) return NULL;

    // Positive fixint 0x00–0x7F
    if (b <= 0x7F) return cJSON_CreateNumber(b);

    // Fixmap 0x80–0x8F
    if ((b & 0xF0) == 0x80) return decode_map(r, b & 0x0F);

    // Fixarray 0x90–0x9F
    if ((b & 0xF0) == 0x90) return decode_array(r, b & 0x0F);

    // Fixstr 0xA0–0xBF
    if ((b & 0xE0) == 0xA0) return decode_str(r, b & 0x1F);

    // Negative fixint 0xE0–0xFF
    if ((b & 0xE0) == 0xE0) return cJSON_CreateNumber((int8_t)b);

    switch (b) {
    case 0xC0: return cJSON_CreateNull();
    case 0xC2: return cJSON_CreateFalse();
    case 0xC3: return cJSON_CreateTrue();

    case 0xC4: { // bin8
        uint8_t n; if (!mr_byte(r, &n)) return NULL;
        return decode_str(r, n);   // treat as string for our use case
    }
    case 0xC5: { // bin16
        uint16_t n; if (!mr_u16(r, &n)) return NULL;
        return decode_str(r, n);
    }
    case 0xC6: { // bin32
        uint32_t n; if (!mr_u32(r, &n)) return NULL;
        return decode_str(r, n);
    }

    case 0xCA: { // float32
        uint8_t raw[4]; if (!mr_read(r, raw, 4)) return NULL;
        uint32_t bits = ((uint32_t)raw[0] << 24) | ((uint32_t)raw[1] << 16) |
                        ((uint32_t)raw[2] << 8) | raw[3];
        float f; memcpy(&f, &bits, 4);
        return cJSON_CreateNumber((double)f);
    }
    case 0xCB: { // float64
        uint64_t bits; if (!mr_u64(r, &bits)) return NULL;
        double d; memcpy(&d, &bits, 8);
        return cJSON_CreateNumber(d);
    }

    case 0xCC: { uint8_t  v; if (!mr_byte(r, &v))  return NULL; return cJSON_CreateNumber(v); }
    case 0xCD: { uint16_t v; if (!mr_u16(r,  &v))  return NULL; return cJSON_CreateNumber(v); }
    case 0xCE: { uint32_t v; if (!mr_u32(r,  &v))  return NULL; return cJSON_CreateNumber(v); }
    case 0xCF: { uint64_t v; if (!mr_u64(r,  &v))  return NULL; return cJSON_CreateNumber((double)v); }

    case 0xD0: { int8_t  v; if (!mr_byte(r,  (uint8_t*)&v)) return NULL; return cJSON_CreateNumber(v); }
    case 0xD1: { int16_t v; if (!mr_u16(r,   (uint16_t*)&v)) return NULL; return cJSON_CreateNumber(v); }
    case 0xD2: { int32_t v; if (!mr_u32(r,   (uint32_t*)&v)) return NULL; return cJSON_CreateNumber(v); }
    case 0xD3: { int64_t v; if (!mr_u64(r,   (uint64_t*)&v)) return NULL; return cJSON_CreateNumber((double)v); }

    case 0xD9: { uint8_t  n; if (!mr_byte(r, &n))  return NULL; return decode_str(r, n); }
    case 0xDA: { uint16_t n; if (!mr_u16(r,  &n))  return NULL; return decode_str(r, n); }
    case 0xDB: { uint32_t n; if (!mr_u32(r,  &n))  return NULL; return decode_str(r, n); }

    case 0xDC: { uint16_t n; if (!mr_u16(r, &n)) return NULL; return decode_array(r, n); }
    case 0xDD: { uint32_t n; if (!mr_u32(r, &n)) return NULL; return decode_array(r, n); }

    case 0xDE: { uint16_t n; if (!mr_u16(r, &n)) return NULL; return decode_map(r, n); }
    case 0xDF: { uint32_t n; if (!mr_u32(r, &n)) return NULL; return decode_map(r, n); }

    default: return NULL;   // unsupported type
    }
}

cJSON *mp_decode(const uint8_t *buf, size_t len) {
    if (!buf || len == 0) return NULL;
    MpReader r = { buf, len, 0 };
    return decode_value(&r);
}

// ---------------------------------------------------------------------------
// Encoder
// ---------------------------------------------------------------------------

typedef struct {
    uint8_t *buf;
    size_t   len;
    size_t   cap;
} MpWriter;

static bool mw_grow(MpWriter *w, size_t need) {
    if (w->len + need <= w->cap) return true;
    size_t newcap = w->cap ? w->cap * 2 : 64;
    while (newcap < w->len + need) newcap *= 2;
    uint8_t *nb = realloc(w->buf, newcap);
    if (!nb) return false;
    w->buf = nb;
    w->cap = newcap;
    return true;
}

static bool mw_byte(MpWriter *w, uint8_t b) {
    if (!mw_grow(w, 1)) return false;
    w->buf[w->len++] = b;
    return true;
}

static bool mw_bytes(MpWriter *w, const uint8_t *data, size_t n) {
    if (!mw_grow(w, n)) return false;
    memcpy(w->buf + w->len, data, n);
    w->len += n;
    return true;
}

static bool mw_u16(MpWriter *w, uint16_t v) {
    uint8_t b[2] = { (uint8_t)(v >> 8), (uint8_t)v };
    return mw_bytes(w, b, 2);
}

static bool mw_u32(MpWriter *w, uint32_t v) {
    uint8_t b[4] = { (uint8_t)(v>>24),(uint8_t)(v>>16),(uint8_t)(v>>8),(uint8_t)v };
    return mw_bytes(w, b, 4);
}

static bool mw_u64(MpWriter *w, uint64_t v) {
    uint8_t b[8] = {
        (uint8_t)(v>>56),(uint8_t)(v>>48),(uint8_t)(v>>40),(uint8_t)(v>>32),
        (uint8_t)(v>>24),(uint8_t)(v>>16),(uint8_t)(v>>8),(uint8_t)v };
    return mw_bytes(w, b, 8);
}

static bool encode_value(MpWriter *w, const cJSON *val);  // forward

static bool encode_str(MpWriter *w, const char *s) {
    size_t len = strlen(s);
    if (len <= 31) {
        if (!mw_byte(w, (uint8_t)(0xA0 | len))) return false;
    } else if (len <= 255) {
        if (!mw_byte(w, 0xD9) || !mw_byte(w, (uint8_t)len)) return false;
    } else if (len <= 65535) {
        if (!mw_byte(w, 0xDA) || !mw_u16(w, (uint16_t)len)) return false;
    } else {
        if (!mw_byte(w, 0xDB) || !mw_u32(w, (uint32_t)len)) return false;
    }
    return mw_bytes(w, (const uint8_t *)s, len);
}

static bool encode_value(MpWriter *w, const cJSON *val) {
    if (!val) return mw_byte(w, 0xC0);  // nil

    if (cJSON_IsNull(val))  return mw_byte(w, 0xC0);
    if (cJSON_IsTrue(val))  return mw_byte(w, 0xC3);
    if (cJSON_IsFalse(val)) return mw_byte(w, 0xC2);

    if (cJSON_IsString(val)) return encode_str(w, val->valuestring ? val->valuestring : "");

    if (cJSON_IsNumber(val)) {
        double d = val->valuedouble;
        // Use integer encoding if the value is an exact integer
        if (d == (int64_t)d) {
            int64_t i = (int64_t)d;
            if (i >= 0 && i <= 127)             return mw_byte(w, (uint8_t)i);
            if (i >= -32 && i < 0)              return mw_byte(w, (uint8_t)(int8_t)i);
            if (i >= 0 && i <= 0xFF)            { return mw_byte(w, 0xCC) && mw_byte(w, (uint8_t)i); }
            if (i >= -128 && i < 0)             { return mw_byte(w, 0xD0) && mw_byte(w, (uint8_t)(int8_t)i); }
            if (i >= 0 && i <= 0xFFFF)          { return mw_byte(w, 0xCD) && mw_u16(w, (uint16_t)i); }
            if (i >= -32768 && i < 0)           { return mw_byte(w, 0xD1) && mw_u16(w, (uint16_t)(int16_t)i); }
            if (i >= 0 && i <= 0xFFFFFFFFL)     { return mw_byte(w, 0xCE) && mw_u32(w, (uint32_t)i); }
            if (i >= -2147483648LL && i < 0)    { return mw_byte(w, 0xD2) && mw_u32(w, (uint32_t)(int32_t)i); }
            // int64/uint64
            if (i >= 0)  { return mw_byte(w, 0xCF) && mw_u64(w, (uint64_t)i); }
            return mw_byte(w, 0xD3) && mw_u64(w, (uint64_t)i);
        }
        // Float64
        uint8_t raw[8];
        memcpy(raw, &d, 8);
        // Big-endian on the wire
        uint8_t be[8] = { raw[7],raw[6],raw[5],raw[4],raw[3],raw[2],raw[1],raw[0] };
        return mw_byte(w, 0xCB) && mw_bytes(w, be, 8);
    }

    if (cJSON_IsArray(val)) {
        int n = cJSON_GetArraySize(val);
        if (n <= 15) {
            if (!mw_byte(w, (uint8_t)(0x90 | n))) return false;
        } else if (n <= 65535) {
            if (!mw_byte(w, 0xDC) || !mw_u16(w, (uint16_t)n)) return false;
        } else {
            if (!mw_byte(w, 0xDD) || !mw_u32(w, (uint32_t)n)) return false;
        }
        const cJSON *item;
        cJSON_ArrayForEach(item, val) {
            if (!encode_value(w, item)) return false;
        }
        return true;
    }

    if (cJSON_IsObject(val)) {
        int n = 0;
        const cJSON *item;
        cJSON_ArrayForEach(item, val) n++;
        if (n <= 15) {
            if (!mw_byte(w, (uint8_t)(0x80 | n))) return false;
        } else if (n <= 65535) {
            if (!mw_byte(w, 0xDE) || !mw_u16(w, (uint16_t)n)) return false;
        } else {
            if (!mw_byte(w, 0xDF) || !mw_u32(w, (uint32_t)n)) return false;
        }
        cJSON_ArrayForEach(item, val) {
            if (!encode_str(w, item->string ? item->string : "")) return false;
            if (!encode_value(w, item)) return false;
        }
        return true;
    }

    return false;  // unknown cJSON type
}

bool mp_encode(const cJSON *val, uint8_t **out, size_t *out_len) {
    MpWriter w = {0};
    if (!encode_value(&w, val)) {
        free(w.buf);
        return false;
    }
    *out     = w.buf;
    *out_len = w.len;
    return true;
}
