#include "unity.h"
#include "protocol.h"
#include <string.h>
#include <stdlib.h>

void setUp(void)    {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// protocol_parse_line
// ---------------------------------------------------------------------------

void test_parse_minimal_command(void) {
    const char *line = "{\"cmd\":\"DrawCircle\"}";
    ParsedCmd *p = protocol_parse_line(line);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_STRING("DrawCircle", p->cmd);
    TEST_ASSERT_NULL(p->id);
    TEST_ASSERT_NULL(p->args);
    protocol_free(p);
}

void test_parse_with_id_and_args(void) {
    const char *line =
        "{\"id\":\"req-1\",\"cmd\":\"ClearBackground\","
        "\"args\":{\"color\":\"RED\"}}";
    ParsedCmd *p = protocol_parse_line(line);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_STRING("req-1", p->id);
    TEST_ASSERT_EQUAL_STRING("ClearBackground", p->cmd);
    TEST_ASSERT_NOT_NULL(p->args);
    cJSON *color = cJSON_GetObjectItemCaseSensitive(p->args, "color");
    TEST_ASSERT_EQUAL_STRING("RED", color->valuestring);
    protocol_free(p);
}

void test_parse_invalid_json_returns_null(void) {
    TEST_ASSERT_NULL(protocol_parse_line("not json at all"));
}

void test_parse_missing_cmd_returns_null(void) {
    TEST_ASSERT_NULL(protocol_parse_line("{\"id\":\"x\",\"args\":{}}"));
}

void test_parse_cmd_not_string_returns_null(void) {
    TEST_ASSERT_NULL(protocol_parse_line("{\"cmd\":42}"));
}

void test_parse_no_id_is_null(void) {
    ParsedCmd *p = protocol_parse_line("{\"cmd\":\"Noop\"}");
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_NULL(p->id);
    protocol_free(p);
}

// ---------------------------------------------------------------------------
// protocol_ok / protocol_error
// ---------------------------------------------------------------------------

void test_ok_response_no_id_no_result(void) {
    char *s = protocol_ok(NULL, NULL);
    TEST_ASSERT_NOT_NULL(s);
    cJSON *r = cJSON_Parse(s);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r, "ok")));
    TEST_ASSERT_NULL(cJSON_GetObjectItemCaseSensitive(r, "id"));
    TEST_ASSERT_NULL(cJSON_GetObjectItemCaseSensitive(r, "result"));
    cJSON_Delete(r);
    free(s);
}

void test_ok_response_with_id_and_result(void) {
    cJSON *result = cJSON_CreateObject();
    cJSON_AddNumberToObject(result, "width", 800);
    char *s = protocol_ok("abc", result);
    cJSON_Delete(result);
    TEST_ASSERT_NOT_NULL(s);
    cJSON *r = cJSON_Parse(s);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL_STRING("abc",
        cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(r, "id")));
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r, "ok")));
    cJSON *res = cJSON_GetObjectItemCaseSensitive(r, "result");
    TEST_ASSERT_NOT_NULL(res);
    TEST_ASSERT_EQUAL_FLOAT(800.0f,
        (float)cJSON_GetObjectItemCaseSensitive(res, "width")->valuedouble);
    cJSON_Delete(r);
    free(s);
}

void test_error_response(void) {
    char *s = protocol_error("e1", "unknown command");
    TEST_ASSERT_NOT_NULL(s);
    cJSON *r = cJSON_Parse(s);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_FALSE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r, "ok")));
    TEST_ASSERT_EQUAL_STRING("e1",
        cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(r, "id")));
    TEST_ASSERT_EQUAL_STRING("unknown command",
        cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(r, "error")));
    cJSON_Delete(r);
    free(s);
}

// ---------------------------------------------------------------------------
// proto_parse_vec2
// ---------------------------------------------------------------------------

void test_parse_vec2_valid(void) {
    // ARRANGE
    cJSON *arr = cJSON_Parse("[100.5, 200.0]");

    // ACT
    float x, y;
    bool ok = proto_parse_vec2(arr, &x, &y);

    // ASSERT
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 100.5f, x);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 200.0f, y);

    cJSON_Delete(arr);
}

void test_parse_vec2_negative_values(void) {
    // ARRANGE
    cJSON *arr = cJSON_Parse("[-50.0, -75.5]");

    // ACT
    float x, y;
    bool ok = proto_parse_vec2(arr, &x, &y);

    // ASSERT
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -50.0f, x);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -75.5f, y);

    cJSON_Delete(arr);
}

void test_parse_vec2_wrong_length_returns_false(void) {
    // ARRANGE
    cJSON *arr3 = cJSON_Parse("[1, 2, 3]");
    cJSON *arr1 = cJSON_Parse("[1]");
    cJSON *empty = cJSON_Parse("[]");
    float x, y;

    // ACT / ASSERT
    TEST_ASSERT_FALSE(proto_parse_vec2(arr3,  &x, &y));
    TEST_ASSERT_FALSE(proto_parse_vec2(arr1,  &x, &y));
    TEST_ASSERT_FALSE(proto_parse_vec2(empty, &x, &y));

    cJSON_Delete(arr3);
    cJSON_Delete(arr1);
    cJSON_Delete(empty);
}

void test_parse_vec2_not_array_returns_false(void) {
    // ARRANGE
    cJSON *obj  = cJSON_Parse("{\"x\":1,\"y\":2}");
    cJSON *num  = cJSON_Parse("42");
    cJSON *null_val = NULL;
    float x, y;

    // ACT / ASSERT
    TEST_ASSERT_FALSE(proto_parse_vec2(obj,      &x, &y));
    TEST_ASSERT_FALSE(proto_parse_vec2(num,      &x, &y));
    TEST_ASSERT_FALSE(proto_parse_vec2(null_val, &x, &y));

    cJSON_Delete(obj);
    cJSON_Delete(num);
}

void test_parse_vec2_non_numeric_element_returns_false(void) {
    // ARRANGE
    cJSON *arr = cJSON_Parse("[1, \"hello\"]");
    float x, y;

    // ACT / ASSERT
    TEST_ASSERT_FALSE(proto_parse_vec2(arr, &x, &y));

    cJSON_Delete(arr);
}

// ---------------------------------------------------------------------------
// proto_parse_rect
// ---------------------------------------------------------------------------

void test_parse_rect_valid(void) {
    // ARRANGE
    cJSON *arr = cJSON_Parse("[10, 20, 100, 50]");

    // ACT
    float x, y, w, h;
    bool ok = proto_parse_rect(arr, &x, &y, &w, &h);

    // ASSERT
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 10.0f,  x);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 20.0f,  y);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 100.0f, w);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 50.0f,  h);

    cJSON_Delete(arr);
}

void test_parse_rect_float_values(void) {
    // ARRANGE
    cJSON *arr = cJSON_Parse("[1.5, 2.5, 300.0, 200.0]");

    // ACT
    float x, y, w, h;
    bool ok = proto_parse_rect(arr, &x, &y, &w, &h);

    // ASSERT
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.5f,   x);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.5f,   y);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 300.0f, w);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 200.0f, h);

    cJSON_Delete(arr);
}

void test_parse_rect_wrong_length_returns_false(void) {
    // ARRANGE
    cJSON *arr3 = cJSON_Parse("[1, 2, 3]");
    cJSON *arr5 = cJSON_Parse("[1, 2, 3, 4, 5]");
    float x, y, w, h;

    // ACT / ASSERT
    TEST_ASSERT_FALSE(proto_parse_rect(arr3, &x, &y, &w, &h));
    TEST_ASSERT_FALSE(proto_parse_rect(arr5, &x, &y, &w, &h));

    cJSON_Delete(arr3);
    cJSON_Delete(arr5);
}

void test_parse_rect_not_array_returns_false(void) {
    // ARRANGE
    cJSON *obj = cJSON_Parse("{\"x\":0}");
    float x, y, w, h;

    // ACT / ASSERT
    TEST_ASSERT_FALSE(proto_parse_rect(obj,  &x, &y, &w, &h));
    TEST_ASSERT_FALSE(proto_parse_rect(NULL, &x, &y, &w, &h));

    cJSON_Delete(obj);
}

// ---------------------------------------------------------------------------
// LineBuffer
// ---------------------------------------------------------------------------

typedef struct { char lines[8][256]; int count; } LineStore;

static void store_line(const char *line, void *userdata) {
    LineStore *ls = userdata;
    if (ls->count < 8) {
        strncpy(ls->lines[ls->count], line, 255);
        ls->lines[ls->count][255] = '\0';
        ls->count++;
    }
}

void test_linebuf_single_line(void) {
    LineBuffer lb;
    linebuf_init(&lb);
    LineStore ls = {0};
    const char *input = "hello world\n";
    int r = linebuf_feed(&lb, input, strlen(input), store_line, &ls);
    TEST_ASSERT_EQUAL_INT(0, r);
    TEST_ASSERT_EQUAL_INT(1, ls.count);
    TEST_ASSERT_EQUAL_STRING("hello world", ls.lines[0]);
}

void test_linebuf_two_lines_one_feed(void) {
    LineBuffer lb;
    linebuf_init(&lb);
    LineStore ls = {0};
    const char *input = "line1\nline2\n";
    linebuf_feed(&lb, input, strlen(input), store_line, &ls);
    TEST_ASSERT_EQUAL_INT(2, ls.count);
    TEST_ASSERT_EQUAL_STRING("line1", ls.lines[0]);
    TEST_ASSERT_EQUAL_STRING("line2", ls.lines[1]);
}

void test_linebuf_partial_then_complete(void) {
    LineBuffer lb;
    linebuf_init(&lb);
    LineStore ls = {0};
    linebuf_feed(&lb, "hell", 4, store_line, &ls);
    TEST_ASSERT_EQUAL_INT(0, ls.count);
    linebuf_feed(&lb, "o\n", 2, store_line, &ls);
    TEST_ASSERT_EQUAL_INT(1, ls.count);
    TEST_ASSERT_EQUAL_STRING("hello", ls.lines[0]);
}

void test_linebuf_overflow_returns_error(void) {
    LineBuffer lb;
    linebuf_init(&lb);
    LineStore ls = {0};
    char *big = calloc(LINEBUF_MAX + 1, 1);
    memset(big, 'x', LINEBUF_MAX);
    int r = linebuf_feed(&lb, big, LINEBUF_MAX, store_line, &ls);
    TEST_ASSERT_EQUAL_INT(-1, r);
    free(big);
}

void test_linebuf_empty_line(void) {
    LineBuffer lb;
    linebuf_init(&lb);
    LineStore ls = {0};
    linebuf_feed(&lb, "\n", 1, store_line, &ls);
    TEST_ASSERT_EQUAL_INT(1, ls.count);
    TEST_ASSERT_EQUAL_STRING("", ls.lines[0]);
}

// ---------------------------------------------------------------------------
// Runner
// ---------------------------------------------------------------------------

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_parse_minimal_command);
    RUN_TEST(test_parse_with_id_and_args);
    RUN_TEST(test_parse_invalid_json_returns_null);
    RUN_TEST(test_parse_missing_cmd_returns_null);
    RUN_TEST(test_parse_cmd_not_string_returns_null);
    RUN_TEST(test_parse_no_id_is_null);
    RUN_TEST(test_ok_response_no_id_no_result);
    RUN_TEST(test_ok_response_with_id_and_result);
    RUN_TEST(test_error_response);
    RUN_TEST(test_parse_vec2_valid);
    RUN_TEST(test_parse_vec2_negative_values);
    RUN_TEST(test_parse_vec2_wrong_length_returns_false);
    RUN_TEST(test_parse_vec2_not_array_returns_false);
    RUN_TEST(test_parse_vec2_non_numeric_element_returns_false);
    RUN_TEST(test_parse_rect_valid);
    RUN_TEST(test_parse_rect_float_values);
    RUN_TEST(test_parse_rect_wrong_length_returns_false);
    RUN_TEST(test_parse_rect_not_array_returns_false);
    RUN_TEST(test_linebuf_single_line);
    RUN_TEST(test_linebuf_two_lines_one_feed);
    RUN_TEST(test_linebuf_partial_then_complete);
    RUN_TEST(test_linebuf_overflow_returns_error);
    RUN_TEST(test_linebuf_empty_line);
    return UNITY_END();
}
