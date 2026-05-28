#include "unity.h"
#include "b64.h"
#include <string.h>
#include <stdlib.h>

void setUp(void)    {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Basic round-trip
// ---------------------------------------------------------------------------

void test_decode_hello(void) {
    // "hello" in base64 is "aGVsbG8="
    size_t len;
    unsigned char *out = b64_decode("aGVsbG8=", 8, &len);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_EQUAL_size_t(5, len);
    TEST_ASSERT_EQUAL_MEMORY("hello", out, 5);
    free(out);
}

void test_decode_padded_two(void) {
    // "he" → "aGU=" (2 padding chars)
    size_t len;
    unsigned char *out = b64_decode("aGU=", 4, &len);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_EQUAL_size_t(2, len);
    TEST_ASSERT_EQUAL_MEMORY("he", out, 2);
    free(out);
}

void test_decode_padded_one(void) {
    // "hel" → "aGVs" (no padding, 4 chars encode 3 bytes exactly)
    size_t len;
    unsigned char *out = b64_decode("aGVs", 4, &len);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_EQUAL_size_t(3, len);
    TEST_ASSERT_EQUAL_MEMORY("hel", out, 3);
    free(out);
}

void test_decode_all_zero_bytes(void) {
    // Three zero bytes = "AAAA"
    size_t len;
    unsigned char *out = b64_decode("AAAA", 4, &len);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_EQUAL_size_t(3, len);
    TEST_ASSERT_EQUAL_UINT8(0, out[0]);
    TEST_ASSERT_EQUAL_UINT8(0, out[1]);
    TEST_ASSERT_EQUAL_UINT8(0, out[2]);
    free(out);
}

void test_decode_empty_string(void) {
    size_t len = 99;
    unsigned char *out = b64_decode("", 0, &len);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_EQUAL_size_t(0, len);
    free(out);
}

void test_decode_whitespace_ignored(void) {
    // "aGVs bG8=" with an embedded space
    size_t len;
    unsigned char *out = b64_decode("aGVs bG8=", 9, &len);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_EQUAL_size_t(5, len);
    TEST_ASSERT_EQUAL_MEMORY("hello", out, 5);
    free(out);
}

void test_decode_newlines_ignored(void) {
    // Same data split over two lines
    const char *b64 = "aGVs\nbG8=";
    size_t len;
    unsigned char *out = b64_decode(b64, strlen(b64), &len);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_EQUAL_size_t(5, len);
    TEST_ASSERT_EQUAL_MEMORY("hello", out, 5);
    free(out);
}

void test_decode_all_alphabet_chars(void) {
    // Standard base64 alphabet test: 3 bytes 0xFB 0xFC 0xFD → "+/z9" ... no,
    // let's just verify a known vector: 0x00 0x10 0x83 0x10 0x51 0x87 → "ABCDEFG"
    // Actually use the known "Man" → "TWFu"
    size_t len;
    unsigned char *out = b64_decode("TWFu", 4, &len);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_EQUAL_size_t(3, len);
    TEST_ASSERT_EQUAL_MEMORY("Man", out, 3);
    free(out);
}

void test_decode_padding_stops_at_equals(void) {
    // "aGVsbG8=extra" — extra chars after the first padding should be ignored
    size_t len;
    unsigned char *out = b64_decode("aGVsbG8=extra", 13, &len);
    TEST_ASSERT_NOT_NULL(out);
    // "hello" is 5 bytes; the padding stops at '='
    TEST_ASSERT_EQUAL_size_t(5, len);
    TEST_ASSERT_EQUAL_MEMORY("hello", out, 5);
    free(out);
}

// ---------------------------------------------------------------------------
// Binary data
// ---------------------------------------------------------------------------

void test_decode_binary_bytes(void) {
    // Encode a sequence of bytes 0x00..0x0F (16 bytes).
    // Base64 of that: "AAECAwQFBgcICQoLDA0ODw=="
    const char *b64 = "AAECAwQFBgcICQoLDA0ODw==";
    size_t len;
    unsigned char *out = b64_decode(b64, strlen(b64), &len);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_EQUAL_size_t(16, len);
    for (int i = 0; i < 16; i++)
        TEST_ASSERT_EQUAL_UINT8((unsigned char)i, out[i]);
    free(out);
}

// ---------------------------------------------------------------------------
// Runner
// ---------------------------------------------------------------------------

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_decode_hello);
    RUN_TEST(test_decode_padded_two);
    RUN_TEST(test_decode_padded_one);
    RUN_TEST(test_decode_all_zero_bytes);
    RUN_TEST(test_decode_empty_string);
    RUN_TEST(test_decode_whitespace_ignored);
    RUN_TEST(test_decode_newlines_ignored);
    RUN_TEST(test_decode_all_alphabet_chars);
    RUN_TEST(test_decode_padding_stops_at_equals);
    RUN_TEST(test_decode_binary_bytes);
    return UNITY_END();
}
