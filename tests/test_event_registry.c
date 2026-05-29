#include "unity.h"
#include "event_registry.h"
#include "protocol.h"
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

static EventRegistry g_reg;

void setUp(void)    { er_init(&g_reg); }
void tearDown(void) { er_destroy(&g_reg); }

// ---------------------------------------------------------------------------
// er_kind_from_name
// ---------------------------------------------------------------------------

void test_kind_from_name_all_known(void) {
    TEST_ASSERT_EQUAL_INT(EVENT_KEY_PRESSED,           er_kind_from_name("KeyPressed"));
    TEST_ASSERT_EQUAL_INT(EVENT_KEY_RELEASED,          er_kind_from_name("KeyReleased"));
    TEST_ASSERT_EQUAL_INT(EVENT_MOUSE_MOVED,           er_kind_from_name("MouseMoved"));
    TEST_ASSERT_EQUAL_INT(EVENT_MOUSE_BUTTON_PRESSED,  er_kind_from_name("MouseButtonPressed"));
    TEST_ASSERT_EQUAL_INT(EVENT_MOUSE_BUTTON_RELEASED, er_kind_from_name("MouseButtonReleased"));
    TEST_ASSERT_EQUAL_INT(EVENT_MOUSE_WHEEL,           er_kind_from_name("MouseWheel"));
    TEST_ASSERT_EQUAL_INT(EVENT_WINDOW_RESIZED,        er_kind_from_name("WindowResized"));
    TEST_ASSERT_EQUAL_INT(EVENT_WINDOW_FOCUSED,        er_kind_from_name("WindowFocused"));
    TEST_ASSERT_EQUAL_INT(EVENT_WINDOW_UNFOCUSED,      er_kind_from_name("WindowUnfocused"));
    TEST_ASSERT_EQUAL_INT(EVENT_WINDOW_CLOSED,         er_kind_from_name("WindowClosed"));
    TEST_ASSERT_EQUAL_INT(EVENT_GESTURE_DETECTED,      er_kind_from_name("GestureDetected"));
    TEST_ASSERT_EQUAL_INT(EVENT_TIMER_FIRED,           er_kind_from_name("TimerFired"));
}

void test_kind_from_name_unknown(void) {
    TEST_ASSERT_EQUAL_INT(-1, er_kind_from_name("BogusEvent"));
    TEST_ASSERT_EQUAL_INT(-1, er_kind_from_name("keyPressed"));  // case-sensitive
    TEST_ASSERT_EQUAL_INT(-1, er_kind_from_name(""));
    TEST_ASSERT_EQUAL_INT(-1, er_kind_from_name(NULL));
}

// ---------------------------------------------------------------------------
// Subscribe / unsubscribe
// ---------------------------------------------------------------------------

void test_subscribe_creates_entry(void) {
    er_subscribe(&g_reg, 10, EVENT_MASK(EVENT_KEY_PRESSED));
    TEST_ASSERT_EQUAL_INT(1, g_reg.count);
    TEST_ASSERT_EQUAL_INT(10, g_reg.subs[0].fd);
    TEST_ASSERT_EQUAL_UINT(EVENT_MASK(EVENT_KEY_PRESSED), g_reg.subs[0].mask);
}

void test_subscribe_merges_mask_for_same_fd(void) {
    er_subscribe(&g_reg, 10, EVENT_MASK(EVENT_KEY_PRESSED));
    er_subscribe(&g_reg, 10, EVENT_MASK(EVENT_MOUSE_MOVED));
    TEST_ASSERT_EQUAL_INT(1, g_reg.count);
    EventMask expected = EVENT_MASK(EVENT_KEY_PRESSED) | EVENT_MASK(EVENT_MOUSE_MOVED);
    TEST_ASSERT_EQUAL_UINT(expected, g_reg.subs[0].mask);
}

void test_subscribe_multiple_fds(void) {
    er_subscribe(&g_reg, 10, EVENT_MASK(EVENT_KEY_PRESSED));
    er_subscribe(&g_reg, 20, EVENT_MASK(EVENT_MOUSE_MOVED));
    TEST_ASSERT_EQUAL_INT(2, g_reg.count);
}

void test_unsubscribe_removes_bits(void) {
    EventMask both = EVENT_MASK(EVENT_KEY_PRESSED) | EVENT_MASK(EVENT_MOUSE_MOVED);
    er_subscribe(&g_reg, 10, both);
    er_unsubscribe(&g_reg, 10, EVENT_MASK(EVENT_MOUSE_MOVED));
    TEST_ASSERT_EQUAL_INT(1, g_reg.count);
    TEST_ASSERT_EQUAL_UINT(EVENT_MASK(EVENT_KEY_PRESSED), g_reg.subs[0].mask);
}

void test_unsubscribe_all_removes_entry(void) {
    er_subscribe(&g_reg, 10, EVENT_MASK(EVENT_KEY_PRESSED));
    er_unsubscribe(&g_reg, 10, EVENT_MASK(EVENT_KEY_PRESSED));
    TEST_ASSERT_EQUAL_INT(0, g_reg.count);
}

void test_remove_fd(void) {
    er_subscribe(&g_reg, 10, EVENT_MASK(EVENT_KEY_PRESSED));
    er_subscribe(&g_reg, 20, EVENT_MASK(EVENT_MOUSE_MOVED));
    er_remove_fd(&g_reg, 10);
    TEST_ASSERT_EQUAL_INT(1, g_reg.count);
    TEST_ASSERT_EQUAL_INT(20, g_reg.subs[0].fd);
}

void test_remove_fd_nonexistent_is_noop(void) {
    er_subscribe(&g_reg, 10, EVENT_MASK(EVENT_KEY_PRESSED));
    er_remove_fd(&g_reg, 99);  // 99 was never subscribed
    TEST_ASSERT_EQUAL_INT(1, g_reg.count);
}

// ---------------------------------------------------------------------------
// er_push
// ---------------------------------------------------------------------------

void test_push_delivers_to_subscriber(void) {
    // ARRANGE
    int fds[2];
    TEST_ASSERT_EQUAL_INT(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds));
    er_subscribe(&g_reg, fds[1], EVENT_MASK(EVENT_KEY_PRESSED));

    // ACT
    er_push(&g_reg, EVENT_KEY_PRESSED,
            "{\"event\":\"KeyPressed\",\"key\":65,\"frame\":1}");

    // ASSERT — other end receives the event
    char buf[256] = {0};
    ssize_t n = recv(fds[0], buf, sizeof(buf) - 1, MSG_DONTWAIT);
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_NOT_NULL(strstr(buf, "KeyPressed"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"key\":65"));

    close(fds[0]);
    close(fds[1]);
}

void test_push_skips_non_subscriber(void) {
    // ARRANGE — fd subscribed to MouseMoved, push KeyPressed
    int fds[2];
    TEST_ASSERT_EQUAL_INT(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds));
    er_subscribe(&g_reg, fds[1], EVENT_MASK(EVENT_MOUSE_MOVED));

    // ACT
    er_push(&g_reg, EVENT_KEY_PRESSED,
            "{\"event\":\"KeyPressed\",\"key\":65,\"frame\":1}");

    // ASSERT — nothing arrives
    char buf[64];
    ssize_t n = recv(fds[0], buf, sizeof(buf), MSG_DONTWAIT);
    TEST_ASSERT_LESS_OR_EQUAL(0, n);

    close(fds[0]);
    close(fds[1]);
}

void test_push_removes_dead_fd(void) {
    // ARRANGE — subscribe a fd, then close both ends
    int fds[2];
    TEST_ASSERT_EQUAL_INT(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds));
    er_subscribe(&g_reg, fds[1], EVENT_MASK(EVENT_KEY_PRESSED));
    close(fds[0]);
    close(fds[1]);

    // ACT — push to the now-dead fd
    er_push(&g_reg, EVENT_KEY_PRESSED,
            "{\"event\":\"KeyPressed\",\"key\":65,\"frame\":1}");

    // ASSERT — dead entry was removed
    TEST_ASSERT_EQUAL_INT(0, g_reg.count);
}

void test_push_delivers_to_multiple_subscribers(void) {
    // ARRANGE — two fds both subscribed to MouseWheel
    int fds_a[2], fds_b[2];
    TEST_ASSERT_EQUAL_INT(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds_a));
    TEST_ASSERT_EQUAL_INT(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds_b));
    er_subscribe(&g_reg, fds_a[1], EVENT_MASK(EVENT_MOUSE_WHEEL));
    er_subscribe(&g_reg, fds_b[1], EVENT_MASK(EVENT_MOUSE_WHEEL));

    // ACT
    er_push(&g_reg, EVENT_MOUSE_WHEEL,
            "{\"event\":\"MouseWheel\",\"move\":1.5,\"frame\":10}");

    // ASSERT — both receive
    char buf[256] = {0};
    TEST_ASSERT_GREATER_THAN(0, recv(fds_a[0], buf, sizeof(buf) - 1, MSG_DONTWAIT));
    TEST_ASSERT_NOT_NULL(strstr(buf, "MouseWheel"));
    memset(buf, 0, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, recv(fds_b[0], buf, sizeof(buf) - 1, MSG_DONTWAIT));
    TEST_ASSERT_NOT_NULL(strstr(buf, "MouseWheel"));

    close(fds_a[0]); close(fds_a[1]);
    close(fds_b[0]); close(fds_b[1]);
}

// ---------------------------------------------------------------------------
// er_handle_cmd — Subscribe / Unsubscribe wire commands
// ---------------------------------------------------------------------------

static int make_socketpair(int fds[2]) {
    return socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
}

void test_handle_subscribe_adds_subscription(void) {
    // ARRANGE
    int fds[2];
    TEST_ASSERT_EQUAL_INT(0, make_socketpair(fds));
    ParsedCmd cmd = {
        .cmd  = "Subscribe",
        .id   = NULL,
        .args = cJSON_Parse("{\"events\":[\"KeyPressed\",\"MouseMoved\"]}")
    };

    // ACT
    bool handled = er_handle_cmd(&g_reg, &cmd, fds[1]);
    cJSON_Delete(cmd.args);

    // ASSERT
    TEST_ASSERT_TRUE(handled);
    TEST_ASSERT_EQUAL_INT(1, g_reg.count);
    EventMask expected = EVENT_MASK(EVENT_KEY_PRESSED) | EVENT_MASK(EVENT_MOUSE_MOVED);
    TEST_ASSERT_EQUAL_UINT(expected, g_reg.subs[0].mask);

    close(fds[0]); close(fds[1]);
}

void test_handle_subscribe_sends_ack_when_id_set(void) {
    // ARRANGE
    int fds[2];
    TEST_ASSERT_EQUAL_INT(0, make_socketpair(fds));
    ParsedCmd cmd = {
        .cmd  = "Subscribe",
        .id   = "s1",
        .args = cJSON_Parse("{\"events\":[\"KeyPressed\"]}")
    };

    // ACT
    er_handle_cmd(&g_reg, &cmd, fds[1]);
    cJSON_Delete(cmd.args);

    // ASSERT — ACK arrives on the other end
    char buf[256] = {0};
    ssize_t n = recv(fds[0], buf, sizeof(buf) - 1, MSG_DONTWAIT);
    TEST_ASSERT_GREATER_THAN(0, n);
    cJSON *r = cJSON_Parse(buf);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r, "ok")));
    TEST_ASSERT_EQUAL_STRING("s1",
        cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(r, "id")));
    cJSON_Delete(r);

    close(fds[0]); close(fds[1]);
}

void test_handle_unsubscribe_removes_events(void) {
    // ARRANGE — subscribe to two events then unsubscribe one
    int fds[2];
    TEST_ASSERT_EQUAL_INT(0, make_socketpair(fds));
    EventMask both = EVENT_MASK(EVENT_KEY_PRESSED) | EVENT_MASK(EVENT_MOUSE_MOVED);
    er_subscribe(&g_reg, fds[1], both);

    ParsedCmd cmd = {
        .cmd  = "Unsubscribe",
        .id   = NULL,
        .args = cJSON_Parse("{\"events\":[\"MouseMoved\"]}")
    };

    // ACT
    er_handle_cmd(&g_reg, &cmd, fds[1]);
    cJSON_Delete(cmd.args);

    // ASSERT — only KeyPressed remains
    TEST_ASSERT_EQUAL_INT(1, g_reg.count);
    TEST_ASSERT_EQUAL_UINT(EVENT_MASK(EVENT_KEY_PRESSED), g_reg.subs[0].mask);

    close(fds[0]); close(fds[1]);
}

void test_handle_subscribe_unknown_event_name_skipped(void) {
    int fds[2];
    TEST_ASSERT_EQUAL_INT(0, make_socketpair(fds));
    ParsedCmd cmd = {
        .cmd  = "Subscribe",
        .id   = NULL,
        .args = cJSON_Parse("{\"events\":[\"NotARealEvent\",\"KeyPressed\"]}")
    };

    // ACT — unknown name is silently skipped; KeyPressed is still added
    er_handle_cmd(&g_reg, &cmd, fds[1]);
    cJSON_Delete(cmd.args);

    TEST_ASSERT_EQUAL_INT(1, g_reg.count);
    TEST_ASSERT_EQUAL_UINT(EVENT_MASK(EVENT_KEY_PRESSED), g_reg.subs[0].mask);

    close(fds[0]); close(fds[1]);
}

void test_handle_non_subscribe_cmd_not_consumed(void) {
    ParsedCmd cmd = { .cmd = "DrawCircle", .id = NULL, .args = NULL };
    bool handled = er_handle_cmd(&g_reg, &cmd, -1);
    TEST_ASSERT_FALSE(handled);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_kind_from_name_all_known);
    RUN_TEST(test_kind_from_name_unknown);
    RUN_TEST(test_subscribe_creates_entry);
    RUN_TEST(test_subscribe_merges_mask_for_same_fd);
    RUN_TEST(test_subscribe_multiple_fds);
    RUN_TEST(test_unsubscribe_removes_bits);
    RUN_TEST(test_unsubscribe_all_removes_entry);
    RUN_TEST(test_remove_fd);
    RUN_TEST(test_remove_fd_nonexistent_is_noop);
    RUN_TEST(test_push_delivers_to_subscriber);
    RUN_TEST(test_push_skips_non_subscriber);
    RUN_TEST(test_push_removes_dead_fd);
    RUN_TEST(test_push_delivers_to_multiple_subscribers);
    RUN_TEST(test_handle_subscribe_adds_subscription);
    RUN_TEST(test_handle_subscribe_sends_ack_when_id_set);
    RUN_TEST(test_handle_unsubscribe_removes_events);
    RUN_TEST(test_handle_subscribe_unknown_event_name_skipped);
    RUN_TEST(test_handle_non_subscribe_cmd_not_consumed);
    return UNITY_END();
}
