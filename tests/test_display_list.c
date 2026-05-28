// Unit tests for the display list module (display_list.h/c).
// No raylib or network dependency — tests the data structure directly.

#include "unity.h"
#include "display_list.h"
#include "protocol.h"
#include <string.h>
#include <stdlib.h>

void setUp(void)    {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/* Build a minimal ParsedCmd without going through protocol_parse_line. */
static ParsedCmd *make_cmd(const char *cmd_name) {
    ParsedCmd *p = calloc(1, sizeof(ParsedCmd));
    p->cmd = strdup(cmd_name);
    return p;
}

static ParsedCmd *make_cmd_with_args(const char *cmd_name, const char *args_json) {
    ParsedCmd *p = make_cmd(cmd_name);
    p->args = cJSON_Parse(args_json);
    return p;
}

/* dl_handle_cmd with conn_fd = -1 so no real socket write occurs. */
#define HANDLE(reg, cmd) dl_handle_cmd((reg), (cmd), -1)

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void test_init_destroy(void) {
    // ARRANGE + ACT
    DisplayListRegistry reg;
    dl_init(&reg);

    // ASSERT
    TEST_ASSERT_EQUAL_INT(0, dl_count(&reg));
    TEST_ASSERT_FALSE(dl_is_recording(&reg));

    dl_destroy(&reg);
}

void test_begin_end_creates_list(void) {
    // ARRANGE
    DisplayListRegistry reg;
    dl_init(&reg);
    ParsedCmd *begin = make_cmd_with_args("DisplayListBegin", "{\"name\":\"main\"}");
    ParsedCmd *end   = make_cmd("DisplayListEnd");

    // ACT
    bool b = HANDLE(&reg, begin);
    bool e = HANDLE(&reg, end);

    // ASSERT
    TEST_ASSERT_TRUE(b);
    TEST_ASSERT_TRUE(e);
    TEST_ASSERT_EQUAL_INT(1, dl_count(&reg));
    TEST_ASSERT_NOT_NULL(dl_get(&reg, "main"));
    TEST_ASSERT_FALSE(dl_is_recording(&reg));

    protocol_free(begin);
    protocol_free(end);
    dl_destroy(&reg);
}

void test_recording_captures_draw_commands(void) {
    // ARRANGE
    DisplayListRegistry reg;
    dl_init(&reg);
    ParsedCmd *begin  = make_cmd_with_args("DisplayListBegin", "{\"name\":\"scene\"}");
    ParsedCmd *circle = make_cmd_with_args("DrawCircle",
        "{\"centerX\":100,\"centerY\":100,\"radius\":50,\"color\":\"RED\"}");
    ParsedCmd *text   = make_cmd_with_args("DrawText",
        "{\"text\":\"hi\",\"posX\":0,\"posY\":0,\"fontSize\":12,\"color\":\"BLACK\"}");
    ParsedCmd *end    = make_cmd("DisplayListEnd");

    // ACT
    HANDLE(&reg, begin);
    bool rc = HANDLE(&reg, circle);
    bool rt = HANDLE(&reg, text);
    HANDLE(&reg, end);

    // ASSERT — draw commands were intercepted
    TEST_ASSERT_TRUE(rc);
    TEST_ASSERT_TRUE(rt);
    const DisplayList *list = dl_get(&reg, "scene");
    TEST_ASSERT_NOT_NULL(list);
    TEST_ASSERT_EQUAL_INT(2, list->count);
    TEST_ASSERT_EQUAL_STRING("DrawCircle", list->cmds[0]->cmd);
    TEST_ASSERT_EQUAL_STRING("DrawText",   list->cmds[1]->cmd);

    protocol_free(begin);
    protocol_free(circle);
    protocol_free(text);
    protocol_free(end);
    dl_destroy(&reg);
}

void test_recorded_cmd_is_deep_copy(void) {
    // ARRANGE
    DisplayListRegistry reg;
    dl_init(&reg);
    ParsedCmd *begin  = make_cmd_with_args("DisplayListBegin", "{\"name\":\"l\"}");
    ParsedCmd *circle = make_cmd_with_args("DrawCircle",
        "{\"centerX\":5,\"centerY\":5,\"radius\":10,\"color\":\"BLUE\"}");
    ParsedCmd *end    = make_cmd("DisplayListEnd");

    // ACT
    HANDLE(&reg, begin);
    HANDLE(&reg, circle);
    HANDLE(&reg, end);
    const ParsedCmd *stored = dl_get(&reg, "l")->cmds[0];

    // ASSERT — stored pointer is not the same object as circle
    TEST_ASSERT_TRUE_MESSAGE(circle != stored, "expected deep copy, not same pointer");
    TEST_ASSERT_EQUAL_STRING(circle->cmd, stored->cmd);

    protocol_free(begin);
    protocol_free(circle);
    protocol_free(end);
    dl_destroy(&reg);
}

void test_non_drawable_not_captured(void) {
    // ARRANGE
    DisplayListRegistry reg;
    dl_init(&reg);
    ParsedCmd *begin  = make_cmd_with_args("DisplayListBegin", "{\"name\":\"l\"}");
    // LoadTexture is a sync resource command — should NOT be recorded.
    ParsedCmd *load   = make_cmd_with_args("LoadTexture", "{\"path\":\"/img.png\"}");
    ParsedCmd *end    = make_cmd("DisplayListEnd");

    // ACT
    HANDLE(&reg, begin);
    bool handled = HANDLE(&reg, load);
    HANDLE(&reg, end);

    // ASSERT — LoadTexture was not captured (falls through to queue)
    TEST_ASSERT_FALSE(handled);
    TEST_ASSERT_EQUAL_INT(0, dl_get(&reg, "l")->count);

    protocol_free(begin);
    protocol_free(load);
    protocol_free(end);
    dl_destroy(&reg);
}

void test_clear_removes_commands(void) {
    // ARRANGE
    DisplayListRegistry reg;
    dl_init(&reg);
    ParsedCmd *begin  = make_cmd_with_args("DisplayListBegin", "{\"name\":\"m\"}");
    ParsedCmd *rect   = make_cmd_with_args("DrawRectangle",
        "{\"posX\":0,\"posY\":0,\"width\":10,\"height\":10,\"color\":\"RED\"}");
    ParsedCmd *end    = make_cmd("DisplayListEnd");
    ParsedCmd *clear  = make_cmd_with_args("DisplayListClear", "{\"name\":\"m\"}");

    HANDLE(&reg, begin);
    HANDLE(&reg, rect);
    HANDLE(&reg, end);
    TEST_ASSERT_EQUAL_INT(1, dl_get(&reg, "m")->count);

    // ACT
    bool handled = HANDLE(&reg, clear);

    // ASSERT
    TEST_ASSERT_TRUE(handled);
    TEST_ASSERT_EQUAL_INT(0, dl_get(&reg, "m")->count);
    TEST_ASSERT_EQUAL_INT(1, dl_count(&reg)); // list still exists

    protocol_free(begin);
    protocol_free(rect);
    protocol_free(end);
    protocol_free(clear);
    dl_destroy(&reg);
}

void test_delete_removes_list(void) {
    // ARRANGE
    DisplayListRegistry reg;
    dl_init(&reg);
    ParsedCmd *begin  = make_cmd_with_args("DisplayListBegin", "{\"name\":\"x\"}");
    ParsedCmd *end    = make_cmd("DisplayListEnd");
    ParsedCmd *del    = make_cmd_with_args("DisplayListDelete", "{\"name\":\"x\"}");

    HANDLE(&reg, begin);
    HANDLE(&reg, end);
    TEST_ASSERT_EQUAL_INT(1, dl_count(&reg));

    // ACT
    bool handled = HANDLE(&reg, del);

    // ASSERT
    TEST_ASSERT_TRUE(handled);
    TEST_ASSERT_EQUAL_INT(0, dl_count(&reg));
    TEST_ASSERT_NULL(dl_get(&reg, "x"));

    protocol_free(begin);
    protocol_free(end);
    protocol_free(del);
    dl_destroy(&reg);
}

void test_set_order_reorders_lists(void) {
    // ARRANGE
    DisplayListRegistry reg;
    dl_init(&reg);
    ParsedCmd *b1 = make_cmd_with_args("DisplayListBegin", "{\"name\":\"bg\"}");
    ParsedCmd *b2 = make_cmd_with_args("DisplayListBegin", "{\"name\":\"fg\"}");
    ParsedCmd *e  = make_cmd("DisplayListEnd");
    ParsedCmd *order = make_cmd_with_args("DisplayListSetOrder",
                                          "{\"names\":[\"fg\",\"bg\"]}");

    HANDLE(&reg, b1); HANDLE(&reg, e);
    HANDLE(&reg, b2); HANDLE(&reg, e);

    // ACT
    bool handled = HANDLE(&reg, order);

    // ASSERT — "fg" should now be first in render order
    TEST_ASSERT_TRUE(handled);
    TEST_ASSERT_EQUAL_INT(2, reg.order_count);
    TEST_ASSERT_EQUAL_STRING("fg", reg.lists[reg.order[0]].name);
    TEST_ASSERT_EQUAL_STRING("bg", reg.lists[reg.order[1]].name);

    protocol_free(b1); protocol_free(b2);
    protocol_free(e);  protocol_free(order);
    dl_destroy(&reg);
}

void test_get_display_lists_response(void) {
    // ARRANGE
    DisplayListRegistry reg;
    dl_init(&reg);
    ParsedCmd *begin = make_cmd_with_args("DisplayListBegin", "{\"name\":\"main\"}");
    ParsedCmd *circ  = make_cmd_with_args("DrawCircleV",
                           "{\"center\":[0,0],\"radius\":5,\"color\":\"RED\"}");
    ParsedCmd *end   = make_cmd("DisplayListEnd");
    HANDLE(&reg, begin);
    HANDLE(&reg, circ);
    HANDLE(&reg, end);

    // Build a GetDisplayLists command with an id so we can check it's handled.
    ParsedCmd *query = make_cmd("GetDisplayLists");
    query->id = strdup("q1");

    // ACT — fd = -1 means response is discarded; we just check return value.
    bool handled = HANDLE(&reg, query);

    // ASSERT
    TEST_ASSERT_TRUE(handled);

    protocol_free(begin); protocol_free(circ); protocol_free(end);
    protocol_free(query);
    dl_destroy(&reg);
}

void test_get_display_list_commands_response(void) {
    // ARRANGE
    DisplayListRegistry reg;
    dl_init(&reg);
    ParsedCmd *begin = make_cmd_with_args("DisplayListBegin", "{\"name\":\"s\"}");
    ParsedCmd *rect  = make_cmd_with_args("DrawRectangle",
        "{\"posX\":1,\"posY\":2,\"width\":3,\"height\":4,\"color\":\"BLUE\"}");
    ParsedCmd *end   = make_cmd("DisplayListEnd");
    HANDLE(&reg, begin); HANDLE(&reg, rect); HANDLE(&reg, end);

    ParsedCmd *query = make_cmd_with_args("GetDisplayListCommands", "{\"name\":\"s\"}");
    query->id = strdup("gc1");

    // ACT
    bool handled = HANDLE(&reg, query);

    // ASSERT
    TEST_ASSERT_TRUE(handled);

    protocol_free(begin); protocol_free(rect); protocol_free(end);
    protocol_free(query);
    dl_destroy(&reg);
}

void test_begin_end_missing_name_is_error(void) {
    // ARRANGE
    DisplayListRegistry reg;
    dl_init(&reg);
    ParsedCmd *begin = make_cmd_with_args("DisplayListBegin", "{}"); // no name

    // ACT
    bool handled = HANDLE(&reg, begin);

    // ASSERT — still consumed (error response sent), but no list created
    TEST_ASSERT_TRUE(handled);
    TEST_ASSERT_EQUAL_INT(0, dl_count(&reg));

    protocol_free(begin);
    dl_destroy(&reg);
}

void test_begin_replaces_existing_list(void) {
    // ARRANGE
    DisplayListRegistry reg;
    dl_init(&reg);
    ParsedCmd *begin1 = make_cmd_with_args("DisplayListBegin", "{\"name\":\"m\"}");
    ParsedCmd *circ   = make_cmd_with_args("DrawCircle",
        "{\"centerX\":0,\"centerY\":0,\"radius\":1,\"color\":\"RED\"}");
    ParsedCmd *end1   = make_cmd("DisplayListEnd");
    ParsedCmd *begin2 = make_cmd_with_args("DisplayListBegin", "{\"name\":\"m\"}");
    ParsedCmd *end2   = make_cmd("DisplayListEnd");

    HANDLE(&reg, begin1); HANDLE(&reg, circ); HANDLE(&reg, end1);
    TEST_ASSERT_EQUAL_INT(1, dl_get(&reg, "m")->count);

    // ACT — begin again on the same list (should clear it)
    HANDLE(&reg, begin2); HANDLE(&reg, end2);

    // ASSERT
    TEST_ASSERT_EQUAL_INT(1, dl_count(&reg)); // still one list
    TEST_ASSERT_EQUAL_INT(0, dl_get(&reg, "m")->count); // but cleared

    protocol_free(begin1); protocol_free(circ); protocol_free(end1);
    protocol_free(begin2); protocol_free(end2);
    dl_destroy(&reg);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_destroy);
    RUN_TEST(test_begin_end_creates_list);
    RUN_TEST(test_recording_captures_draw_commands);
    RUN_TEST(test_recorded_cmd_is_deep_copy);
    RUN_TEST(test_non_drawable_not_captured);
    RUN_TEST(test_clear_removes_commands);
    RUN_TEST(test_delete_removes_list);
    RUN_TEST(test_set_order_reorders_lists);
    RUN_TEST(test_get_display_lists_response);
    RUN_TEST(test_get_display_list_commands_response);
    RUN_TEST(test_begin_end_missing_name_is_error);
    RUN_TEST(test_begin_replaces_existing_list);
    return UNITY_END();
}
