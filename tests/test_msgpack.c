#include "unity.h"
#include "msgpack.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

void setUp(void)    {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Round-trip helper: encode val → bytes, decode bytes → val2,
// compare JSON strings.
// ---------------------------------------------------------------------------
static void round_trip(cJSON *val) {
    uint8_t *mp = NULL; size_t mp_len = 0;
    TEST_ASSERT_TRUE_MESSAGE(mp_encode(val, &mp, &mp_len), "encode failed");
    TEST_ASSERT_NOT_NULL(mp);

    cJSON *val2 = mp_decode(mp, mp_len);
    free(mp);
    TEST_ASSERT_NOT_NULL_MESSAGE(val2, "decode returned NULL");

    char *s1 = cJSON_PrintUnformatted(val);
    char *s2 = cJSON_PrintUnformatted(val2);
    TEST_ASSERT_NOT_NULL(s1);
    TEST_ASSERT_NOT_NULL(s2);
    TEST_ASSERT_EQUAL_STRING(s1, s2);
    free(s1); free(s2);
    cJSON_Delete(val2);
}

// ---------------------------------------------------------------------------
// Primitives
// ---------------------------------------------------------------------------

void test_null(void) {
    cJSON *v = cJSON_CreateNull();
    round_trip(v);
    cJSON_Delete(v);
}

void test_true(void) {
    cJSON *v = cJSON_CreateTrue();
    round_trip(v);
    cJSON_Delete(v);
}

void test_false(void) {
    cJSON *v = cJSON_CreateFalse();
    round_trip(v);
    cJSON_Delete(v);
}

void test_fixint_zero(void) {
    cJSON *v = cJSON_CreateNumber(0);
    round_trip(v);
    cJSON_Delete(v);
}

void test_fixint_127(void) {
    cJSON *v = cJSON_CreateNumber(127);
    round_trip(v);
    cJSON_Delete(v);
}

void test_negative_fixint(void) {
    cJSON *v = cJSON_CreateNumber(-1);
    round_trip(v);
    cJSON_Delete(v);
}

void test_negative_fixint_min(void) {
    cJSON *v = cJSON_CreateNumber(-32);
    round_trip(v);
    cJSON_Delete(v);
}

void test_uint8(void) {
    cJSON *v = cJSON_CreateNumber(200);
    round_trip(v);
    cJSON_Delete(v);
}

void test_uint16(void) {
    cJSON *v = cJSON_CreateNumber(60000);
    round_trip(v);
    cJSON_Delete(v);
}

void test_uint32(void) {
    cJSON *v = cJSON_CreateNumber(3000000000LL);
    round_trip(v);
    cJSON_Delete(v);
}

void test_int8(void) {
    cJSON *v = cJSON_CreateNumber(-100);
    round_trip(v);
    cJSON_Delete(v);
}

void test_int16(void) {
    cJSON *v = cJSON_CreateNumber(-1000);
    round_trip(v);
    cJSON_Delete(v);
}

void test_int32(void) {
    cJSON *v = cJSON_CreateNumber(-2000000);
    round_trip(v);
    cJSON_Delete(v);
}

void test_float(void) {
    // Float round-trip may lose precision; just check decode doesn't crash
    // and value is close.
    uint8_t *mp = NULL; size_t mp_len = 0;
    cJSON *v = cJSON_CreateNumber(3.14159265);
    TEST_ASSERT_TRUE(mp_encode(v, &mp, &mp_len));
    cJSON *v2 = mp_decode(mp, mp_len);
    free(mp);
    TEST_ASSERT_NOT_NULL(v2);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 3.14159265f, (float)v2->valuedouble);
    cJSON_Delete(v); cJSON_Delete(v2);
}

// ---------------------------------------------------------------------------
// Strings
// ---------------------------------------------------------------------------

void test_empty_string(void) {
    cJSON *v = cJSON_CreateString("");
    round_trip(v);
    cJSON_Delete(v);
}

void test_fixstr(void) {
    cJSON *v = cJSON_CreateString("DrawCircle");
    round_trip(v);
    cJSON_Delete(v);
}

void test_str8(void) {
    // 32-character string — just past fixstr limit
    cJSON *v = cJSON_CreateString("12345678901234567890123456789012");
    round_trip(v);
    cJSON_Delete(v);
}

void test_long_string(void) {
    char buf[300];
    memset(buf, 'A', 299);
    buf[299] = '\0';
    cJSON *v = cJSON_CreateString(buf);
    round_trip(v);
    cJSON_Delete(v);
}

// ---------------------------------------------------------------------------
// Arrays
// ---------------------------------------------------------------------------

void test_empty_array(void) {
    cJSON *v = cJSON_CreateArray();
    round_trip(v);
    cJSON_Delete(v);
}

void test_fixarray(void) {
    // [400, 300] — typical Vector2
    cJSON *v = cJSON_CreateArray();
    cJSON_AddItemToArray(v, cJSON_CreateNumber(400));
    cJSON_AddItemToArray(v, cJSON_CreateNumber(300));
    round_trip(v);
    cJSON_Delete(v);
}

void test_nested_array(void) {
    // [[255,0,0,255]] — color
    cJSON *inner = cJSON_CreateArray();
    cJSON_AddItemToArray(inner, cJSON_CreateNumber(255));
    cJSON_AddItemToArray(inner, cJSON_CreateNumber(0));
    cJSON_AddItemToArray(inner, cJSON_CreateNumber(0));
    cJSON_AddItemToArray(inner, cJSON_CreateNumber(255));
    cJSON *outer = cJSON_CreateArray();
    cJSON_AddItemToArray(outer, inner);
    round_trip(outer);
    cJSON_Delete(outer);
}

// ---------------------------------------------------------------------------
// Objects / Maps
// ---------------------------------------------------------------------------

void test_empty_object(void) {
    cJSON *v = cJSON_CreateObject();
    round_trip(v);
    cJSON_Delete(v);
}

void test_draw_command_envelope(void) {
    // {"cmd":"DrawCircle","args":{"posX":400,"posY":300,"radius":50,"color":"RED"}}
    cJSON *args = cJSON_CreateObject();
    cJSON_AddNumberToObject(args, "posX", 400);
    cJSON_AddNumberToObject(args, "posY", 300);
    cJSON_AddNumberToObject(args, "radius", 50);
    cJSON_AddStringToObject(args, "color", "RED");

    cJSON *cmd = cJSON_CreateObject();
    cJSON_AddStringToObject(cmd, "cmd", "DrawCircle");
    cJSON_AddItemToObject(cmd, "args", args);

    round_trip(cmd);
    cJSON_Delete(cmd);
}

void test_sync_command_envelope(void) {
    // {"id":"abc123","cmd":"GetScreenWidth"}
    cJSON *cmd = cJSON_CreateObject();
    cJSON_AddStringToObject(cmd, "id",  "abc123");
    cJSON_AddStringToObject(cmd, "cmd", "GetScreenWidth");
    round_trip(cmd);
    cJSON_Delete(cmd);
}

void test_response_ok(void) {
    // {"id":"abc123","ok":true,"result":{"width":800}}
    cJSON *result = cJSON_CreateObject();
    cJSON_AddNumberToObject(result, "width", 800);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "id", "abc123");
    cJSON_AddTrueToObject(resp, "ok");
    cJSON_AddItemToObject(resp, "result", result);
    round_trip(resp);
    cJSON_Delete(resp);
}

void test_decode_null_returns_null(void) {
    TEST_ASSERT_NULL(mp_decode(NULL, 0));
    TEST_ASSERT_NULL(mp_decode(NULL, 4));
}

void test_decode_empty_returns_null(void) {
    const uint8_t buf[] = {};
    TEST_ASSERT_NULL(mp_decode(buf, 0));
}

void test_decode_truncated_returns_null(void) {
    // fixmap(1) but no key/value bytes
    const uint8_t buf[] = { 0x81 };
    TEST_ASSERT_NULL(mp_decode(buf, 1));
}

// ---------------------------------------------------------------------------
// Runner
// ---------------------------------------------------------------------------

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_null);
    RUN_TEST(test_true);
    RUN_TEST(test_false);
    RUN_TEST(test_fixint_zero);
    RUN_TEST(test_fixint_127);
    RUN_TEST(test_negative_fixint);
    RUN_TEST(test_negative_fixint_min);
    RUN_TEST(test_uint8);
    RUN_TEST(test_uint16);
    RUN_TEST(test_uint32);
    RUN_TEST(test_int8);
    RUN_TEST(test_int16);
    RUN_TEST(test_int32);
    RUN_TEST(test_float);
    RUN_TEST(test_empty_string);
    RUN_TEST(test_fixstr);
    RUN_TEST(test_str8);
    RUN_TEST(test_long_string);
    RUN_TEST(test_empty_array);
    RUN_TEST(test_fixarray);
    RUN_TEST(test_nested_array);
    RUN_TEST(test_empty_object);
    RUN_TEST(test_draw_command_envelope);
    RUN_TEST(test_sync_command_envelope);
    RUN_TEST(test_response_ok);
    RUN_TEST(test_decode_null_returns_null);
    RUN_TEST(test_decode_empty_returns_null);
    RUN_TEST(test_decode_truncated_returns_null);
    return UNITY_END();
}
