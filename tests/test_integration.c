// Integration tests — start a real TCP server, connect as a client,
// send commands, and verify JSON responses.
//
// These tests exercise the full server.c + queue.c + protocol.c path
// without requiring a raylib window (commands are queued but never drained
// by a render loop; the queue has ample capacity for the small number of
// test commands).

#include "unity.h"
#include "server.h"
#include "queue.h"
#include "protocol.h"
#include "display_list.h"
#include "upload_registry.h"
#include "event_registry.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <stdio.h>

#define TEST_PORT 17878

static CmdQueue             g_queue;
static ServerState          g_server;
static DisplayListRegistry  g_dl_registry;
static UploadRegistry       g_ur_registry;
static EventRegistry        g_ev_registry;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static int connect_client(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(TEST_PORT);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    for (int i = 0; i < 20; i++) {
        if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) return fd;
        usleep(5000);
    }
    close(fd);
    return -1;
}

static void send_line(int fd, const char *json) {
    char buf[4096];
    int  len = snprintf(buf, sizeof(buf), "%s\n", json);
    send(fd, buf, (size_t)len, 0);
}

static int recv_line(int fd, char *buf, size_t size) {
    size_t pos = 0;
    while (pos < size - 1) {
        char    c;
        ssize_t n = recv(fd, &c, 1, 0);
        if (n <= 0) return -1;
        if (c == '\n') break;
        buf[pos++] = c;
    }
    buf[pos] = '\0';
    return (int)pos;
}

// Send a command with id, read response, assert ok:true. Returns false on failure.
static bool assert_cmd_ok(int fd, const char *id, const char *json_with_id) {
    send_line(fd, json_with_id);
    char buf[512];
    if (recv_line(fd, buf, sizeof(buf)) <= 0) return false;
    cJSON *r = cJSON_Parse(buf);
    if (!r) return false;
    bool ok = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r, "ok"));
    const char *rid = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(r, "id"));
    bool id_match = rid && strcmp(rid, id) == 0;
    cJSON_Delete(r);
    return ok && id_match;
}

static void assert_ok(const char *buf) {
    cJSON *r = cJSON_Parse(buf);
    TEST_ASSERT_NOT_NULL_MESSAGE(r, "response is not valid JSON");
    TEST_ASSERT_TRUE_MESSAGE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r, "ok")),
                             "ok is not true");
    cJSON_Delete(r);
}

void setUp(void) {
    CmdEntry e;
    while (cmdq_pop(&g_queue, &e)) protocol_free(e.parsed);
}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Extra helpers for Phase 4
// ---------------------------------------------------------------------------

/* Receive a JSON response that contains a specific string value for a key. */
static bool recv_ok_with_id(int fd, const char *expected_id) {
    char buf[512];
    if (recv_line(fd, buf, sizeof(buf)) <= 0) return false;
    cJSON *r = cJSON_Parse(buf);
    if (!r) return false;
    bool ok      = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r, "ok"));
    const char *id = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(r, "id"));
    bool match   = id && strcmp(id, expected_id) == 0;
    cJSON_Delete(r);
    return ok && match;
}

// ---------------------------------------------------------------------------
// Phase 1: connectivity and basic commands
// ---------------------------------------------------------------------------

void test_connect_to_server(void) {
    int fd = connect_client();
    TEST_ASSERT_NOT_EQUAL_MESSAGE(-1, fd, "failed to connect to test server");
    close(fd);
}

void test_clear_background_ack(void) {
    int fd = connect_client();
    TEST_ASSERT_NOT_EQUAL(-1, fd);
    send_line(fd, "{\"id\":\"t1\",\"cmd\":\"ClearBackground\",\"args\":{\"color\":\"RAYWHITE\"}}");
    char buf[512];
    int n = recv_line(fd, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, n);
    assert_ok(buf);
    cJSON *r = cJSON_Parse(buf);
    TEST_ASSERT_EQUAL_STRING("t1",
        cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(r, "id")));
    cJSON_Delete(r);
    close(fd);
}

void test_draw_circle_ack(void) {
    int fd = connect_client();
    TEST_ASSERT_NOT_EQUAL(-1, fd);
    send_line(fd, "{\"id\":\"dc1\",\"cmd\":\"DrawCircle\","
                  "\"args\":{\"centerX\":100,\"centerY\":100,\"radius\":50,\"color\":\"RED\"}}");
    char buf[512];
    recv_line(fd, buf, sizeof(buf));
    assert_ok(buf);
    close(fd);
}

void test_draw_rectangle_ack(void) {
    int fd = connect_client();
    TEST_ASSERT_NOT_EQUAL(-1, fd);
    send_line(fd, "{\"id\":\"dr1\",\"cmd\":\"DrawRectangle\","
                  "\"args\":{\"posX\":10,\"posY\":20,\"width\":100,\"height\":50,"
                  "\"color\":[0,200,100,255]}}");
    char buf[512];
    recv_line(fd, buf, sizeof(buf));
    assert_ok(buf);
    close(fd);
}

void test_draw_text_ack(void) {
    int fd = connect_client();
    TEST_ASSERT_NOT_EQUAL(-1, fd);
    send_line(fd, "{\"id\":\"dt1\",\"cmd\":\"DrawText\","
                  "\"args\":{\"text\":\"hello\",\"posX\":10,\"posY\":10,"
                  "\"fontSize\":20,\"color\":\"BLACK\"}}");
    char buf[512];
    recv_line(fd, buf, sizeof(buf));
    assert_ok(buf);
    close(fd);
}

void test_begin_end_drawing_ack(void) {
    int fd = connect_client();
    TEST_ASSERT_NOT_EQUAL(-1, fd);
    char buf[512];
    send_line(fd, "{\"id\":\"bd1\",\"cmd\":\"BeginDrawing\"}");
    recv_line(fd, buf, sizeof(buf));
    assert_ok(buf);
    send_line(fd, "{\"id\":\"ed1\",\"cmd\":\"EndDrawing\"}");
    recv_line(fd, buf, sizeof(buf));
    assert_ok(buf);
    close(fd);
}

void test_no_id_no_response(void) {
    int fd = connect_client();
    TEST_ASSERT_NOT_EQUAL(-1, fd);
    send_line(fd, "{\"cmd\":\"ClearBackground\",\"args\":{\"color\":\"BLACK\"}}");
    send_line(fd, "{\"id\":\"sync\",\"cmd\":\"DrawFPS\",\"args\":{\"posX\":0,\"posY\":0}}");
    char buf[512];
    recv_line(fd, buf, sizeof(buf));
    cJSON *r = cJSON_Parse(buf);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL_STRING("sync",
        cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(r, "id")));
    cJSON_Delete(r);
    close(fd);
}

void test_invalid_json_returns_error(void) {
    int fd = connect_client();
    TEST_ASSERT_NOT_EQUAL(-1, fd);
    send_line(fd, "this is not json");
    char buf[512];
    recv_line(fd, buf, sizeof(buf));
    cJSON *r = cJSON_Parse(buf);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_FALSE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r, "ok")));
    cJSON_Delete(r);
    close(fd);
}

void test_multiple_clients_independent(void) {
    int fd1 = connect_client();
    int fd2 = connect_client();
    TEST_ASSERT_NOT_EQUAL(-1, fd1);
    TEST_ASSERT_NOT_EQUAL(-1, fd2);
    char buf[512];
    send_line(fd1, "{\"id\":\"c1\",\"cmd\":\"DrawCircle\","
                   "\"args\":{\"centerX\":10,\"centerY\":10,\"radius\":5,\"color\":\"BLUE\"}}");
    send_line(fd2, "{\"id\":\"c2\",\"cmd\":\"DrawCircle\","
                   "\"args\":{\"centerX\":20,\"centerY\":20,\"radius\":5,\"color\":\"GREEN\"}}");
    recv_line(fd1, buf, sizeof(buf));
    cJSON *r1 = cJSON_Parse(buf);
    recv_line(fd2, buf, sizeof(buf));
    cJSON *r2 = cJSON_Parse(buf);
    TEST_ASSERT_NOT_NULL(r1);
    TEST_ASSERT_NOT_NULL(r2);
    TEST_ASSERT_EQUAL_STRING("c1",
        cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(r1, "id")));
    TEST_ASSERT_EQUAL_STRING("c2",
        cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(r2, "id")));
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r1, "ok")));
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r2, "ok")));
    cJSON_Delete(r1);
    cJSON_Delete(r2);
    close(fd1);
    close(fd2);
}

void test_command_reaches_queue(void) {
    int before = cmdq_count(&g_queue);
    int fd = connect_client();
    TEST_ASSERT_NOT_EQUAL(-1, fd);
    send_line(fd, "{\"id\":\"q1\",\"cmd\":\"DrawCircle\","
                  "\"args\":{\"centerX\":0,\"centerY\":0,\"radius\":1,\"color\":\"RED\"}}");
    char buf[512];
    recv_line(fd, buf, sizeof(buf));
    int after = cmdq_count(&g_queue);
    TEST_ASSERT_EQUAL_INT(before + 1, after);
    close(fd);
}

// ---------------------------------------------------------------------------
// Phase 2: line commands
// ---------------------------------------------------------------------------

void test_line_commands(void) {
    int fd = connect_client();
    TEST_ASSERT_NOT_EQUAL(-1, fd);

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "l1",
        "{\"id\":\"l1\",\"cmd\":\"DrawLine\","
        "\"args\":{\"startPosX\":0,\"startPosY\":0,\"endPosX\":100,\"endPosY\":100,"
        "\"color\":\"BLACK\"}}"));

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "l2",
        "{\"id\":\"l2\",\"cmd\":\"DrawLineV\","
        "\"args\":{\"startPos\":[0,0],\"endPos\":[100,100],\"color\":\"BLACK\"}}"));

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "l3",
        "{\"id\":\"l3\",\"cmd\":\"DrawLineEx\","
        "\"args\":{\"startPos\":[0,0],\"endPos\":[100,100],\"thick\":3.0,\"color\":\"RED\"}}"));

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "l4",
        "{\"id\":\"l4\",\"cmd\":\"DrawLineStrip\","
        "\"args\":{\"points\":[[0,0],[50,50],[100,0],[150,50]],\"color\":\"BLUE\"}}"));

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "l5",
        "{\"id\":\"l5\",\"cmd\":\"DrawLineBezier\","
        "\"args\":{\"startPos\":[0,100],\"endPos\":[200,100],\"thick\":2.0,\"color\":\"GREEN\"}}"));

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "l7",
        "{\"id\":\"l7\",\"cmd\":\"DrawPixel\","
        "\"args\":{\"posX\":50,\"posY\":50,\"color\":\"RED\"}}"));

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "l8",
        "{\"id\":\"l8\",\"cmd\":\"DrawPixelV\","
        "\"args\":{\"position\":[60,60],\"color\":\"BLUE\"}}"));

    close(fd);
}

// ---------------------------------------------------------------------------
// Phase 2: circle commands
// ---------------------------------------------------------------------------

void test_circle_commands(void) {
    int fd = connect_client();
    TEST_ASSERT_NOT_EQUAL(-1, fd);

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "ci1",
        "{\"id\":\"ci1\",\"cmd\":\"DrawCircleV\","
        "\"args\":{\"center\":[200,200],\"radius\":50,\"color\":\"RED\"}}"));

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "ci2",
        "{\"id\":\"ci2\",\"cmd\":\"DrawCircleGradient\","
        "\"args\":{\"centerX\":200,\"centerY\":200,\"radius\":60,"
        "\"inner\":\"WHITE\",\"outer\":\"BLUE\"}}"));

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "ci3",
        "{\"id\":\"ci3\",\"cmd\":\"DrawCircleSector\","
        "\"args\":{\"center\":[200,200],\"radius\":50,"
        "\"startAngle\":0,\"endAngle\":180,\"segments\":16,\"color\":\"GREEN\"}}"));

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "ci4",
        "{\"id\":\"ci4\",\"cmd\":\"DrawCircleSectorLines\","
        "\"args\":{\"center\":[200,200],\"radius\":50,"
        "\"startAngle\":0,\"endAngle\":270,\"segments\":16,\"color\":\"ORANGE\"}}"));

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "ci5",
        "{\"id\":\"ci5\",\"cmd\":\"DrawCircleLines\","
        "\"args\":{\"centerX\":200,\"centerY\":200,\"radius\":50,\"color\":\"PURPLE\"}}"));

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "ci6",
        "{\"id\":\"ci6\",\"cmd\":\"DrawCircleLinesV\","
        "\"args\":{\"center\":[200,200],\"radius\":55,\"color\":\"MAROON\"}}"));

    close(fd);
}

// ---------------------------------------------------------------------------
// Phase 2: ellipse and ring commands
// ---------------------------------------------------------------------------

void test_ellipse_and_ring_commands(void) {
    int fd = connect_client();
    TEST_ASSERT_NOT_EQUAL(-1, fd);

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "e1",
        "{\"id\":\"e1\",\"cmd\":\"DrawEllipse\","
        "\"args\":{\"centerX\":200,\"centerY\":200,"
        "\"radiusH\":80,\"radiusV\":40,\"color\":\"SKYBLUE\"}}"));

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "e3",
        "{\"id\":\"e3\",\"cmd\":\"DrawEllipseLines\","
        "\"args\":{\"centerX\":200,\"centerY\":200,"
        "\"radiusH\":80,\"radiusV\":40,\"color\":\"DARKBLUE\"}}"));

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "r1",
        "{\"id\":\"r1\",\"cmd\":\"DrawRing\","
        "\"args\":{\"center\":[300,300],\"innerRadius\":30,\"outerRadius\":60,"
        "\"startAngle\":0,\"endAngle\":360,\"segments\":32,\"color\":\"GOLD\"}}"));

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "r2",
        "{\"id\":\"r2\",\"cmd\":\"DrawRingLines\","
        "\"args\":{\"center\":[300,300],\"innerRadius\":30,\"outerRadius\":60,"
        "\"startAngle\":0,\"endAngle\":360,\"segments\":32,\"color\":\"ORANGE\"}}"));

    close(fd);
}

// ---------------------------------------------------------------------------
// Phase 2: rectangle variants
// ---------------------------------------------------------------------------

void test_rectangle_commands(void) {
    int fd = connect_client();
    TEST_ASSERT_NOT_EQUAL(-1, fd);

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "rv1",
        "{\"id\":\"rv1\",\"cmd\":\"DrawRectangleV\","
        "\"args\":{\"position\":[10,10],\"size\":[100,50],\"color\":\"RED\"}}"));

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "rr1",
        "{\"id\":\"rr1\",\"cmd\":\"DrawRectangleRec\","
        "\"args\":{\"rec\":[10,10,100,50],\"color\":\"GREEN\"}}"));

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "rp1",
        "{\"id\":\"rp1\",\"cmd\":\"DrawRectanglePro\","
        "\"args\":{\"rec\":[100,100,80,40],\"origin\":[40,20],"
        "\"rotation\":45,\"color\":\"BLUE\"}}"));

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "rgv",
        "{\"id\":\"rgv\",\"cmd\":\"DrawRectangleGradientV\","
        "\"args\":{\"posX\":10,\"posY\":10,\"width\":100,\"height\":60,"
        "\"top\":\"WHITE\",\"bottom\":\"BLACK\"}}"));

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "rgh",
        "{\"id\":\"rgh\",\"cmd\":\"DrawRectangleGradientH\","
        "\"args\":{\"posX\":10,\"posY\":10,\"width\":100,\"height\":60,"
        "\"left\":\"RED\",\"right\":\"BLUE\"}}"));

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "rge",
        "{\"id\":\"rge\",\"cmd\":\"DrawRectangleGradientEx\","
        "\"args\":{\"rec\":[10,10,100,60],"
        "\"topLeft\":\"RED\",\"bottomLeft\":\"BLUE\","
        "\"bottomRight\":\"GREEN\",\"topRight\":\"YELLOW\"}}"));

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "rl1",
        "{\"id\":\"rl1\",\"cmd\":\"DrawRectangleLines\","
        "\"args\":{\"posX\":10,\"posY\":10,\"width\":100,\"height\":50,\"color\":\"BLACK\"}}"));

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "rle",
        "{\"id\":\"rle\",\"cmd\":\"DrawRectangleLinesEx\","
        "\"args\":{\"rec\":[10,10,100,50],\"lineThick\":3.0,\"color\":\"MAROON\"}}"));

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "rro",
        "{\"id\":\"rro\",\"cmd\":\"DrawRectangleRounded\","
        "\"args\":{\"rec\":[10,10,100,50],\"roundness\":0.3,\"segments\":8,\"color\":\"PURPLE\"}}"));

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "rrl",
        "{\"id\":\"rrl\",\"cmd\":\"DrawRectangleRoundedLines\","
        "\"args\":{\"rec\":[10,10,100,50],\"roundness\":0.3,\"segments\":8,\"color\":\"VIOLET\"}}"));

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "rrx",
        "{\"id\":\"rrx\",\"cmd\":\"DrawRectangleRoundedLinesEx\","
        "\"args\":{\"rec\":[10,10,100,50],\"roundness\":0.3,\"segments\":8,"
        "\"lineThick\":2.0,\"color\":\"DARKPURPLE\"}}"));

    close(fd);
}

// ---------------------------------------------------------------------------
// Phase 2: triangle commands
// ---------------------------------------------------------------------------

void test_triangle_commands(void) {
    int fd = connect_client();
    TEST_ASSERT_NOT_EQUAL(-1, fd);

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "tr1",
        "{\"id\":\"tr1\",\"cmd\":\"DrawTriangle\","
        "\"args\":{\"v1\":[200,100],\"v2\":[100,300],\"v3\":[300,300],\"color\":\"RED\"}}"));

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "tr2",
        "{\"id\":\"tr2\",\"cmd\":\"DrawTriangleLines\","
        "\"args\":{\"v1\":[200,100],\"v2\":[100,300],\"v3\":[300,300],\"color\":\"BLUE\"}}"));

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "tr4",
        "{\"id\":\"tr4\",\"cmd\":\"DrawTriangleFan\","
        "\"args\":{\"points\":[[200,200],[100,100],[300,100],[350,200],[300,300],[100,300]],"
        "\"color\":\"ORANGE\"}}"));

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "tr5",
        "{\"id\":\"tr5\",\"cmd\":\"DrawTriangleStrip\","
        "\"args\":{\"points\":[[100,100],[200,100],[100,200],[200,200],[100,300]],"
        "\"color\":\"GOLD\"}}"));

    close(fd);
}

// ---------------------------------------------------------------------------
// Phase 2: polygon commands
// ---------------------------------------------------------------------------

void test_polygon_commands(void) {
    int fd = connect_client();
    TEST_ASSERT_NOT_EQUAL(-1, fd);

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "po1",
        "{\"id\":\"po1\",\"cmd\":\"DrawPoly\","
        "\"args\":{\"center\":[300,300],\"sides\":6,\"radius\":80,"
        "\"rotation\":0,\"color\":\"SKYBLUE\"}}"));

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "po2",
        "{\"id\":\"po2\",\"cmd\":\"DrawPolyLines\","
        "\"args\":{\"center\":[300,300],\"sides\":5,\"radius\":60,"
        "\"rotation\":18,\"color\":\"MAROON\"}}"));

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "po3",
        "{\"id\":\"po3\",\"cmd\":\"DrawPolyLinesEx\","
        "\"args\":{\"center\":[300,300],\"sides\":8,\"radius\":70,"
        "\"rotation\":22.5,\"lineThick\":3.0,\"color\":\"DARKGREEN\"}}"));

    close(fd);
}

// ---------------------------------------------------------------------------
// Phase 2: spline commands
// ---------------------------------------------------------------------------

void test_spline_commands(void) {
    int fd = connect_client();
    TEST_ASSERT_NOT_EQUAL(-1, fd);

    const char *pts6 = "[[50,300],[150,100],[250,300],[350,100],[450,300],[550,100]]";

    char buf[1024];

    snprintf(buf, sizeof(buf),
        "{\"id\":\"sp1\",\"cmd\":\"DrawSplineLinear\","
        "\"args\":{\"points\":%s,\"thick\":2.0,\"color\":\"RED\"}}", pts6);
    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "sp1", buf));

    snprintf(buf, sizeof(buf),
        "{\"id\":\"sp2\",\"cmd\":\"DrawSplineBasis\","
        "\"args\":{\"points\":%s,\"thick\":2.0,\"color\":\"GREEN\"}}", pts6);
    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "sp2", buf));

    snprintf(buf, sizeof(buf),
        "{\"id\":\"sp3\",\"cmd\":\"DrawSplineCatmullRom\","
        "\"args\":{\"points\":%s,\"thick\":2.0,\"color\":\"BLUE\"}}", pts6);
    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "sp3", buf));

    snprintf(buf, sizeof(buf),
        "{\"id\":\"sp4\",\"cmd\":\"DrawSplineBezierQuadratic\","
        "\"args\":{\"points\":[[50,300],[150,100],[250,300]],"
        "\"thick\":2.0,\"color\":\"ORANGE\"}}");
    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "sp4", buf));

    snprintf(buf, sizeof(buf),
        "{\"id\":\"sp5\",\"cmd\":\"DrawSplineBezierCubic\","
        "\"args\":{\"points\":%s,\"thick\":2.0,\"color\":\"PURPLE\"}}", pts6);
    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "sp5", buf));

    close(fd);
}

// ---------------------------------------------------------------------------
// Phase 2: mode stack commands
// ---------------------------------------------------------------------------

void test_mode_commands(void) {
    int fd = connect_client();
    TEST_ASSERT_NOT_EQUAL(-1, fd);

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "m1",
        "{\"id\":\"m1\",\"cmd\":\"BeginMode2D\","
        "\"args\":{\"camera\":{\"offset\":[400,300],\"target\":[0,0],"
        "\"rotation\":0,\"zoom\":1.5}}}"));

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "m2",
        "{\"id\":\"m2\",\"cmd\":\"EndMode2D\"}"));

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "m3",
        "{\"id\":\"m3\",\"cmd\":\"BeginBlendMode\",\"args\":{\"mode\":1}}"));

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "m4",
        "{\"id\":\"m4\",\"cmd\":\"EndBlendMode\"}"));

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "m5",
        "{\"id\":\"m5\",\"cmd\":\"BeginScissorMode\","
        "\"args\":{\"x\":100,\"y\":100,\"width\":200,\"height\":150}}"));

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "m6",
        "{\"id\":\"m6\",\"cmd\":\"EndScissorMode\"}"));

    close(fd);
}

// ---------------------------------------------------------------------------
// Phase 3: sync command protocol behaviour
// ---------------------------------------------------------------------------

// Set a receive timeout on a socket.  Returns true on success.
static bool set_recv_timeout(int fd, int ms) {
    struct timeval tv = { .tv_sec = ms / 1000, .tv_usec = (ms % 1000) * 1000 };
    return setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0;
}

void test_sync_cmd_no_optimistic_ack(void) {
    // ARRANGE
    int fd = connect_client();
    TEST_ASSERT_NOT_EQUAL(-1, fd);
    // Short receive timeout so recv() fails fast if no data arrives.
    TEST_ASSERT_TRUE(set_recv_timeout(fd, 150));

    // ACT — send a sync command (LoadTexture) with an id
    send_line(fd, "{\"id\":\"lt1\",\"cmd\":\"LoadTexture\",\"args\":{\"path\":\"/nonexistent.png\"}}");

    // ASSERT — no optimistic ACK arrives within the timeout window
    char buf[512];
    int n = recv_line(fd, buf, sizeof(buf));
    // The recv should time out (n < 0) because the main thread never executes
    // the command in headless test mode.
    TEST_ASSERT_LESS_THAN(0, n);

    close(fd);
}

void test_fire_and_forget_gets_immediate_ack(void) {
    // ARRANGE
    int fd = connect_client();
    TEST_ASSERT_NOT_EQUAL(-1, fd);
    TEST_ASSERT_TRUE(set_recv_timeout(fd, 150));

    // ACT — send a fire-and-forget command with an id
    send_line(fd, "{\"id\":\"dc1\",\"cmd\":\"DrawCircle\","
                  "\"args\":{\"centerX\":10,\"centerY\":10,\"radius\":5,\"color\":\"RED\"}}");

    // ASSERT — optimistic ACK arrives immediately
    char buf[512];
    int n = recv_line(fd, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, n);
    cJSON *r = cJSON_Parse(buf);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r, "ok")));
    TEST_ASSERT_EQUAL_STRING("dc1",
        cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(r, "id")));
    cJSON_Delete(r);
    close(fd);
}

void test_unload_invalid_handle_no_crash(void) {
    // UnloadTexture with an invalid handle is fire-and-forget; server logs a
    // warning but does not crash and still accepts subsequent commands.
    int fd = connect_client();
    TEST_ASSERT_NOT_EQUAL(-1, fd);

    // No id — fire-and-forget, no response expected.
    send_line(fd, "{\"cmd\":\"UnloadTexture\",\"args\":{\"handle\":9999}}");

    // Follow-up command should still get an ACK.
    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "ok1",
        "{\"id\":\"ok1\",\"cmd\":\"DrawFPS\",\"args\":{\"posX\":0,\"posY\":0}}"));
    close(fd);
}

void test_draw_texture_invalid_handle_no_crash(void) {
    // DrawTexture with an invalid handle — server warns but does not crash.
    int fd = connect_client();
    TEST_ASSERT_NOT_EQUAL(-1, fd);

    // No id — fire-and-forget.
    send_line(fd, "{\"cmd\":\"DrawTexture\",\"args\":{\"handle\":42,\"posX\":0,\"posY\":0,\"tint\":\"WHITE\"}}");

    // Server is still alive.
    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "alive",
        "{\"id\":\"alive\",\"cmd\":\"DrawText\","
        "\"args\":{\"text\":\"ok\",\"posX\":0,\"posY\":0,\"fontSize\":10,\"color\":\"BLACK\"}}"));
    close(fd);
}

void test_upload_texture_bad_data_no_crash(void) {
    // UploadTexture with a non-existent path and malformed base64.
    // Because it is a sync command, no optimistic ACK is sent; but since the
    // main thread never runs in this test harness, we only verify the server
    // doesn't crash and still serves subsequent requests on a different fd.
    int fd1 = connect_client();
    TEST_ASSERT_NOT_EQUAL(-1, fd1);
    send_line(fd1, "{\"id\":\"u1\",\"cmd\":\"UploadTexture\","
                   "\"args\":{\"fileType\":\".png\",\"data\":\"!!!invalid!!!\"}}");
    close(fd1);

    // New connection still works fine.
    int fd2 = connect_client();
    TEST_ASSERT_NOT_EQUAL(-1, fd2);
    TEST_ASSERT_TRUE(assert_cmd_ok(fd2, "s1",
        "{\"id\":\"s1\",\"cmd\":\"ClearBackground\",\"args\":{\"color\":\"BLACK\"}}"));
    close(fd2);
}

// ---------------------------------------------------------------------------
// Phase 3b: chunked upload
// ---------------------------------------------------------------------------

void test_begin_upload_returns_upload_id(void) {
    // ARRANGE
    int fd = connect_client();
    TEST_ASSERT_NOT_EQUAL(-1, fd);

    // ACT
    send_line(fd, "{\"id\":\"bu1\",\"cmd\":\"BeginUpload\","
                  "\"args\":{\"name\":\"test\",\"fileType\":\".png\",\"totalBytes\":128}}");
    char buf[512];
    int n = recv_line(fd, buf, sizeof(buf));

    // ASSERT — immediate response with uploadId (connection thread)
    TEST_ASSERT_GREATER_THAN(0, n);
    cJSON *r = cJSON_Parse(buf);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r, "ok")));
    const char *uid = cJSON_GetStringValue(
        cJSON_GetObjectItemCaseSensitive(
            cJSON_GetObjectItemCaseSensitive(r, "result"), "uploadId"));
    TEST_ASSERT_NOT_NULL(uid);
    TEST_ASSERT_GREATER_THAN(0, (int)strlen(uid));
    cJSON_Delete(r);

    close(fd);
}

void test_upload_chunk_ack(void) {
    // ARRANGE — begin upload then send a chunk
    int fd = connect_client();
    TEST_ASSERT_NOT_EQUAL(-1, fd);

    send_line(fd, "{\"id\":\"bu2\",\"cmd\":\"BeginUpload\","
                  "\"args\":{\"name\":\"t\",\"fileType\":\".png\",\"totalBytes\":3}}");
    char buf[512];
    recv_line(fd, buf, sizeof(buf));
    cJSON *r = cJSON_Parse(buf);
    TEST_ASSERT_NOT_NULL(r);
    const char *uid = cJSON_GetStringValue(
        cJSON_GetObjectItemCaseSensitive(
            cJSON_GetObjectItemCaseSensitive(r, "result"), "uploadId"));
    TEST_ASSERT_NOT_NULL(uid);

    // "QUJD" is base64 for "ABC" (3 bytes).
    char chunk_cmd[512];
    snprintf(chunk_cmd, sizeof(chunk_cmd),
             "{\"id\":\"uc1\",\"cmd\":\"UploadChunk\","
             "\"args\":{\"uploadId\":\"%s\",\"seq\":0,\"data\":\"QUJD\"}}", uid);
    cJSON_Delete(r);

    // ACT
    send_line(fd, chunk_cmd);
    recv_line(fd, buf, sizeof(buf));

    // ASSERT — immediate ACK
    cJSON *r2 = cJSON_Parse(buf);
    TEST_ASSERT_NOT_NULL(r2);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r2, "ok")));
    cJSON_Delete(r2);

    close(fd);
}

void test_abort_upload_ack(void) {
    // ARRANGE
    int fd = connect_client();
    TEST_ASSERT_NOT_EQUAL(-1, fd);

    send_line(fd, "{\"id\":\"bu3\",\"cmd\":\"BeginUpload\","
                  "\"args\":{\"name\":\"ab\",\"fileType\":\".png\",\"totalBytes\":64}}");
    char buf[512];
    recv_line(fd, buf, sizeof(buf));
    cJSON *r = cJSON_Parse(buf);
    const char *uid = cJSON_GetStringValue(
        cJSON_GetObjectItemCaseSensitive(
            cJSON_GetObjectItemCaseSensitive(r, "result"), "uploadId"));

    char abort_cmd[256];
    snprintf(abort_cmd, sizeof(abort_cmd),
             "{\"id\":\"ab1\",\"cmd\":\"AbortUpload\","
             "\"args\":{\"uploadId\":\"%s\"}}", uid);
    cJSON_Delete(r);

    // ACT
    send_line(fd, abort_cmd);
    recv_line(fd, buf, sizeof(buf));

    // ASSERT — immediate ACK
    cJSON *r2 = cJSON_Parse(buf);
    TEST_ASSERT_NOT_NULL(r2);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r2, "ok")));
    cJSON_Delete(r2);

    close(fd);
}

void test_list_uploads_shows_pending(void) {
    // ARRANGE — begin an upload and leave it open
    int fd = connect_client();
    TEST_ASSERT_NOT_EQUAL(-1, fd);

    send_line(fd, "{\"id\":\"bu4\",\"cmd\":\"BeginUpload\","
                  "\"args\":{\"name\":\"pending\",\"fileType\":\".png\",\"totalBytes\":512}}");
    char buf[512];
    recv_line(fd, buf, sizeof(buf));
    cJSON_Delete(cJSON_Parse(buf));

    // ACT
    send_line(fd, "{\"id\":\"lu1\",\"cmd\":\"ListUploads\"}");
    recv_line(fd, buf, sizeof(buf));

    // ASSERT — at least one upload listed
    cJSON *r = cJSON_Parse(buf);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r, "ok")));
    cJSON *uploads = cJSON_GetObjectItemCaseSensitive(
        cJSON_GetObjectItemCaseSensitive(r, "result"), "uploads");
    TEST_ASSERT_NOT_NULL(uploads);
    TEST_ASSERT_GREATER_OR_EQUAL(1, cJSON_GetArraySize(uploads));
    cJSON_Delete(r);

    close(fd);
}

void test_commit_upload_is_sync(void) {
    // CommitUpload goes through the queue (main thread needed for OpenGL).
    // In headless test mode it never executes, so no response arrives within
    // the timeout — just like LoadTexture.
    int fd = connect_client();
    TEST_ASSERT_NOT_EQUAL(-1, fd);
    TEST_ASSERT_TRUE(set_recv_timeout(fd, 150));

    send_line(fd, "{\"id\":\"cm1\",\"cmd\":\"CommitUpload\","
                  "\"args\":{\"uploadId\":\"u-999\",\"type\":\"texture\"}}");

    char buf[512];
    int n = recv_line(fd, buf, sizeof(buf));
    TEST_ASSERT_LESS_THAN(0, n);  // timeout — no optimistic ACK

    close(fd);
}

// ---------------------------------------------------------------------------
// Phase 4: display lists
// ---------------------------------------------------------------------------

void test_display_list_begin_end_ack(void) {
    // ARRANGE
    int fd = connect_client();
    TEST_ASSERT_NOT_EQUAL(-1, fd);

    // ACT + ASSERT — DisplayListBegin, a draw command, and DisplayListEnd all
    // get fire-and-forget ACKs (handled directly in connection thread).
    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "dl1",
        "{\"id\":\"dl1\",\"cmd\":\"DisplayListBegin\",\"args\":{\"name\":\"t\"}}"));

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "dl2",
        "{\"id\":\"dl2\",\"cmd\":\"DrawCircle\","
        "\"args\":{\"centerX\":100,\"centerY\":100,\"radius\":50,\"color\":\"RED\"}}"));

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "dl3",
        "{\"id\":\"dl3\",\"cmd\":\"DisplayListEnd\"}"));

    close(fd);
}

void test_display_list_commands_stored(void) {
    // ARRANGE
    int fd = connect_client();
    TEST_ASSERT_NOT_EQUAL(-1, fd);

    // Build a list with two draw commands.
    send_line(fd, "{\"id\":\"b1\",\"cmd\":\"DisplayListBegin\",\"args\":{\"name\":\"s1\"}}");
    TEST_ASSERT_TRUE(recv_ok_with_id(fd, "b1"));

    send_line(fd, "{\"id\":\"d1\",\"cmd\":\"DrawCircle\","
                  "\"args\":{\"centerX\":10,\"centerY\":10,\"radius\":5,\"color\":\"RED\"}}");
    TEST_ASSERT_TRUE(recv_ok_with_id(fd, "d1"));

    send_line(fd, "{\"id\":\"d2\",\"cmd\":\"ClearBackground\","
                  "\"args\":{\"color\":\"BLACK\"}}");
    TEST_ASSERT_TRUE(recv_ok_with_id(fd, "d2"));

    send_line(fd, "{\"id\":\"e1\",\"cmd\":\"DisplayListEnd\"}");
    TEST_ASSERT_TRUE(recv_ok_with_id(fd, "e1"));

    // ACT — query the stored commands.
    send_line(fd, "{\"id\":\"gc1\",\"cmd\":\"GetDisplayListCommands\","
                  "\"args\":{\"name\":\"s1\"}}");
    char buf[2048];
    int n = recv_line(fd, buf, sizeof(buf));

    // ASSERT — response contains two commands.
    TEST_ASSERT_GREATER_THAN(0, n);
    cJSON *r = cJSON_Parse(buf);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r, "ok")));
    cJSON *result = cJSON_GetObjectItemCaseSensitive(r, "result");
    TEST_ASSERT_NOT_NULL(result);
    cJSON *cmds = cJSON_GetObjectItemCaseSensitive(result, "commands");
    TEST_ASSERT_NOT_NULL(cmds);
    TEST_ASSERT_EQUAL_INT(2, cJSON_GetArraySize(cmds));
    const char *first = cJSON_GetStringValue(
        cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(cmds, 0), "cmd"));
    TEST_ASSERT_EQUAL_STRING("DrawCircle", first);
    cJSON_Delete(r);

    close(fd);
}

void test_display_list_update_replaces_commands(void) {
    // ARRANGE — record a list, then re-record it with different commands.
    int fd = connect_client();
    TEST_ASSERT_NOT_EQUAL(-1, fd);

    // First recording: one DrawCircle.
    send_line(fd, "{\"id\":\"b1\",\"cmd\":\"DisplayListBegin\",\"args\":{\"name\":\"upd\"}}");
    recv_ok_with_id(fd, "b1");
    send_line(fd, "{\"id\":\"d1\",\"cmd\":\"DrawCircle\","
                  "\"args\":{\"centerX\":0,\"centerY\":0,\"radius\":1,\"color\":\"RED\"}}");
    recv_ok_with_id(fd, "d1");
    send_line(fd, "{\"id\":\"e1\",\"cmd\":\"DisplayListEnd\"}");
    recv_ok_with_id(fd, "e1");

    // ACT — second recording: two DrawText commands.
    send_line(fd, "{\"id\":\"b2\",\"cmd\":\"DisplayListBegin\",\"args\":{\"name\":\"upd\"}}");
    recv_ok_with_id(fd, "b2");
    send_line(fd, "{\"id\":\"d2\",\"cmd\":\"DrawText\","
                  "\"args\":{\"text\":\"a\",\"posX\":0,\"posY\":0,\"fontSize\":12,\"color\":\"BLACK\"}}");
    recv_ok_with_id(fd, "d2");
    send_line(fd, "{\"id\":\"d3\",\"cmd\":\"DrawText\","
                  "\"args\":{\"text\":\"b\",\"posX\":0,\"posY\":20,\"fontSize\":12,\"color\":\"BLACK\"}}");
    recv_ok_with_id(fd, "d3");
    send_line(fd, "{\"id\":\"e2\",\"cmd\":\"DisplayListEnd\"}");
    recv_ok_with_id(fd, "e2");

    // ASSERT — query returns two commands (the updated list).
    send_line(fd, "{\"id\":\"gc1\",\"cmd\":\"GetDisplayListCommands\","
                  "\"args\":{\"name\":\"upd\"}}");
    char buf[2048];
    recv_line(fd, buf, sizeof(buf));
    cJSON *r = cJSON_Parse(buf);
    TEST_ASSERT_NOT_NULL(r);
    cJSON *cmds = cJSON_GetObjectItemCaseSensitive(
        cJSON_GetObjectItemCaseSensitive(r, "result"), "commands");
    TEST_ASSERT_NOT_NULL(cmds);
    TEST_ASSERT_EQUAL_INT(2, cJSON_GetArraySize(cmds));
    cJSON_Delete(r);

    close(fd);
}

void test_display_list_clear(void) {
    // ARRANGE
    int fd = connect_client();
    TEST_ASSERT_NOT_EQUAL(-1, fd);

    send_line(fd, "{\"id\":\"b1\",\"cmd\":\"DisplayListBegin\",\"args\":{\"name\":\"cl\"}}");
    recv_ok_with_id(fd, "b1");
    send_line(fd, "{\"id\":\"d1\",\"cmd\":\"DrawCircle\","
                  "\"args\":{\"centerX\":0,\"centerY\":0,\"radius\":1,\"color\":\"BLUE\"}}");
    recv_ok_with_id(fd, "d1");
    send_line(fd, "{\"id\":\"e1\",\"cmd\":\"DisplayListEnd\"}");
    recv_ok_with_id(fd, "e1");

    // ACT
    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "clr",
        "{\"id\":\"clr\",\"cmd\":\"DisplayListClear\",\"args\":{\"name\":\"cl\"}}"));

    // ASSERT — list exists but has zero commands.
    send_line(fd, "{\"id\":\"gc1\",\"cmd\":\"GetDisplayListCommands\","
                  "\"args\":{\"name\":\"cl\"}}");
    char buf[2048];
    recv_line(fd, buf, sizeof(buf));
    cJSON *r = cJSON_Parse(buf);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r, "ok")));
    cJSON *cmds = cJSON_GetObjectItemCaseSensitive(
        cJSON_GetObjectItemCaseSensitive(r, "result"), "commands");
    TEST_ASSERT_EQUAL_INT(0, cJSON_GetArraySize(cmds));
    cJSON_Delete(r);

    close(fd);
}

void test_display_list_delete(void) {
    // ARRANGE
    int fd = connect_client();
    TEST_ASSERT_NOT_EQUAL(-1, fd);

    send_line(fd, "{\"id\":\"b1\",\"cmd\":\"DisplayListBegin\",\"args\":{\"name\":\"del\"}}");
    recv_ok_with_id(fd, "b1");
    send_line(fd, "{\"id\":\"e1\",\"cmd\":\"DisplayListEnd\"}");
    recv_ok_with_id(fd, "e1");

    // ACT
    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "dl",
        "{\"id\":\"dl\",\"cmd\":\"DisplayListDelete\",\"args\":{\"name\":\"del\"}}"));

    // ASSERT — list is gone; GetDisplayListCommands returns error.
    send_line(fd, "{\"id\":\"gc1\",\"cmd\":\"GetDisplayListCommands\","
                  "\"args\":{\"name\":\"del\"}}");
    char buf[512];
    recv_line(fd, buf, sizeof(buf));
    cJSON *r = cJSON_Parse(buf);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_FALSE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r, "ok")));
    cJSON_Delete(r);

    close(fd);
}

void test_get_display_lists(void) {
    // ARRANGE — create two named lists
    int fd = connect_client();
    TEST_ASSERT_NOT_EQUAL(-1, fd);

    send_line(fd, "{\"id\":\"b1\",\"cmd\":\"DisplayListBegin\",\"args\":{\"name\":\"aa\"}}");
    recv_ok_with_id(fd, "b1");
    send_line(fd, "{\"id\":\"e1\",\"cmd\":\"DisplayListEnd\"}");
    recv_ok_with_id(fd, "e1");

    send_line(fd, "{\"id\":\"b2\",\"cmd\":\"DisplayListBegin\",\"args\":{\"name\":\"bb\"}}");
    recv_ok_with_id(fd, "b2");
    send_line(fd, "{\"id\":\"e2\",\"cmd\":\"DisplayListEnd\"}");
    recv_ok_with_id(fd, "e2");

    // ACT
    send_line(fd, "{\"id\":\"gl1\",\"cmd\":\"GetDisplayLists\"}");
    char buf[2048];
    int n = recv_line(fd, buf, sizeof(buf));

    // ASSERT — response contains both lists
    TEST_ASSERT_GREATER_THAN(0, n);
    cJSON *r = cJSON_Parse(buf);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r, "ok")));
    cJSON *lists = cJSON_GetObjectItemCaseSensitive(
        cJSON_GetObjectItemCaseSensitive(r, "result"), "lists");
    TEST_ASSERT_NOT_NULL(lists);
    // At least two lists (others from earlier tests may also be present).
    TEST_ASSERT_GREATER_OR_EQUAL(2, cJSON_GetArraySize(lists));
    cJSON_Delete(r);

    close(fd);
}

// ---------------------------------------------------------------------------
// Phase 1 (gap fill): missing window management commands
// ---------------------------------------------------------------------------

void test_window_management_commands(void) {
    int fd = connect_client();
    TEST_ASSERT_NOT_EQUAL(-1, fd);

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "wm1",
        "{\"id\":\"wm1\",\"cmd\":\"SetWindowMinSize\",\"args\":{\"width\":200,\"height\":150}}"));

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "wm2",
        "{\"id\":\"wm2\",\"cmd\":\"SetWindowMaxSize\",\"args\":{\"width\":3840,\"height\":2160}}"));

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "wm3",
        "{\"id\":\"wm3\",\"cmd\":\"SetWindowState\",\"args\":{\"flags\":0}}"));

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "wm4",
        "{\"id\":\"wm4\",\"cmd\":\"ClearWindowState\",\"args\":{\"flags\":0}}"));

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "wm5",
        "{\"id\":\"wm5\",\"cmd\":\"SetWindowOpacity\",\"args\":{\"opacity\":1.0}}"));

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "wm6",
        "{\"id\":\"wm6\",\"cmd\":\"SetWindowFocused\"}"));

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "wm7",
        "{\"id\":\"wm7\",\"cmd\":\"ToggleFullscreen\"}"));

    // Toggle back so the window doesn't stay in an odd state during tests.
    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "wm7b",
        "{\"id\":\"wm7b\",\"cmd\":\"ToggleFullscreen\"}"));

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "wm8",
        "{\"id\":\"wm8\",\"cmd\":\"MaximizeWindow\"}"));

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "wm9",
        "{\"id\":\"wm9\",\"cmd\":\"RestoreWindow\"}"));

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "wm10",
        "{\"id\":\"wm10\",\"cmd\":\"MinimizeWindow\"}"));

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "wm11",
        "{\"id\":\"wm11\",\"cmd\":\"RestoreWindow\"}"));

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "wm12",
        "{\"id\":\"wm12\",\"cmd\":\"ToggleBorderlessWindowed\"}"));

    TEST_ASSERT_TRUE(assert_cmd_ok(fd, "wm12b",
        "{\"id\":\"wm12b\",\"cmd\":\"ToggleBorderlessWindowed\"}"));

    close(fd);
}

// ---------------------------------------------------------------------------
// Phase 5: introspection — all queries are sync (no optimistic ACK)
// ---------------------------------------------------------------------------

void test_window_query_is_sync(void) {
    // ARRANGE
    int fd = connect_client();
    TEST_ASSERT_NOT_EQUAL(-1, fd);
    TEST_ASSERT_TRUE(set_recv_timeout(fd, 150));

    // ACT — send a window-state query (sync; needs main thread)
    send_line(fd, "{\"id\":\"gsw\",\"cmd\":\"GetScreenWidth\"}");

    // ASSERT — no optimistic ACK in headless mode
    char buf[512];
    int n = recv_line(fd, buf, sizeof(buf));
    TEST_ASSERT_LESS_THAN(0, n);

    close(fd);
}

void test_fps_query_is_sync(void) {
    int fd = connect_client();
    TEST_ASSERT_NOT_EQUAL(-1, fd);
    TEST_ASSERT_TRUE(set_recv_timeout(fd, 150));

    send_line(fd, "{\"id\":\"gfps\",\"cmd\":\"GetFPS\"}");

    char buf[512];
    int n = recv_line(fd, buf, sizeof(buf));
    TEST_ASSERT_LESS_THAN(0, n);

    close(fd);
}

void test_input_query_is_sync(void) {
    // GetMousePosition is a sync command handled by the main thread.
    int fd = connect_client();
    TEST_ASSERT_NOT_EQUAL(-1, fd);
    TEST_ASSERT_TRUE(set_recv_timeout(fd, 150));

    send_line(fd, "{\"id\":\"gmp\",\"cmd\":\"GetMousePosition\"}");

    char buf[512];
    int n = recv_line(fd, buf, sizeof(buf));
    TEST_ASSERT_LESS_THAN(0, n);

    close(fd);
}

void test_is_key_query_is_sync(void) {
    int fd = connect_client();
    TEST_ASSERT_NOT_EQUAL(-1, fd);
    TEST_ASSERT_TRUE(set_recv_timeout(fd, 150));

    send_line(fd, "{\"id\":\"ikp\",\"cmd\":\"IsKeyPressed\",\"args\":{\"key\":65}}");

    char buf[512];
    int n = recv_line(fd, buf, sizeof(buf));
    TEST_ASSERT_LESS_THAN(0, n);

    close(fd);
}

void test_list_handles_is_sync(void) {
    int fd = connect_client();
    TEST_ASSERT_NOT_EQUAL(-1, fd);
    TEST_ASSERT_TRUE(set_recv_timeout(fd, 150));

    send_line(fd, "{\"id\":\"lh1\",\"cmd\":\"ListHandles\"}");

    char buf[512];
    int n = recv_line(fd, buf, sizeof(buf));
    TEST_ASSERT_LESS_THAN(0, n);

    close(fd);
}

void test_get_server_info_is_sync(void) {
    int fd = connect_client();
    TEST_ASSERT_NOT_EQUAL(-1, fd);
    TEST_ASSERT_TRUE(set_recv_timeout(fd, 150));

    send_line(fd, "{\"id\":\"gsi\",\"cmd\":\"GetServerInfo\"}");

    char buf[512];
    int n = recv_line(fd, buf, sizeof(buf));
    TEST_ASSERT_LESS_THAN(0, n);

    close(fd);
}

void test_get_texture_info_invalid_is_sync(void) {
    // GetTextureInfo is sync; with invalid handle it still waits for main thread.
    int fd = connect_client();
    TEST_ASSERT_NOT_EQUAL(-1, fd);
    TEST_ASSERT_TRUE(set_recv_timeout(fd, 150));

    send_line(fd, "{\"id\":\"gti\",\"cmd\":\"GetTextureInfo\",\"args\":{\"handle\":999}}");

    char buf[512];
    int n = recv_line(fd, buf, sizeof(buf));
    TEST_ASSERT_LESS_THAN(0, n);

    close(fd);
}

// ---------------------------------------------------------------------------
// Phase 6: event streaming (Subscribe / Unsubscribe handled in conn thread)
// ---------------------------------------------------------------------------

void test_subscribe_sends_ack(void) {
    int fd = connect_client();
    TEST_ASSERT_NOT_EQUAL(-1, fd);
    TEST_ASSERT_TRUE(set_recv_timeout(fd, 150));

    // ARRANGE + ACT
    send_line(fd, "{\"id\":\"ev1\",\"cmd\":\"Subscribe\",\"args\":{\"events\":[\"KeyPressed\"]}}");

    // ASSERT — ACK arrives immediately (connection-thread command)
    char buf[256] = {0};
    int n = recv_line(fd, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, n);
    cJSON *r = cJSON_Parse(buf);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r, "ok")));
    TEST_ASSERT_EQUAL_STRING("ev1",
        cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(r, "id")));
    cJSON_Delete(r);

    close(fd);
}

void test_unsubscribe_sends_ack(void) {
    int fd = connect_client();
    TEST_ASSERT_NOT_EQUAL(-1, fd);
    TEST_ASSERT_TRUE(set_recv_timeout(fd, 150));

    // ARRANGE — subscribe first
    send_line(fd, "{\"id\":\"ev2\",\"cmd\":\"Subscribe\",\"args\":{\"events\":[\"MouseMoved\"]}}");
    char buf[256] = {0};
    recv_line(fd, buf, sizeof(buf));  // consume ACK

    // ACT — unsubscribe
    send_line(fd, "{\"id\":\"ev3\",\"cmd\":\"Unsubscribe\",\"args\":{\"events\":[\"MouseMoved\"]}}");

    // ASSERT
    memset(buf, 0, sizeof(buf));
    int n = recv_line(fd, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, n);
    cJSON *r = cJSON_Parse(buf);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r, "ok")));
    TEST_ASSERT_EQUAL_STRING("ev3",
        cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(r, "id")));
    cJSON_Delete(r);

    close(fd);
}

void test_subscribe_no_id_no_ack(void) {
    int fd = connect_client();
    TEST_ASSERT_NOT_EQUAL(-1, fd);
    TEST_ASSERT_TRUE(set_recv_timeout(fd, 150));

    // ARRANGE + ACT — no id field
    send_line(fd, "{\"cmd\":\"Subscribe\",\"args\":{\"events\":[\"KeyReleased\"]}}");

    // ASSERT — nothing received (fire-and-forget)
    char buf[64];
    int n = recv_line(fd, buf, sizeof(buf));
    TEST_ASSERT_LESS_THAN(0, n);

    close(fd);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(void) {
    signal(SIGPIPE, SIG_IGN);

    cmdq_init(&g_queue);
    dl_init(&g_dl_registry);
    ur_init(&g_ur_registry);
    er_init(&g_ev_registry);
    server_init(&g_server, TEST_PORT, &g_queue, &g_dl_registry, &g_ur_registry, &g_ev_registry);
    if (!server_start(&g_server)) {
        fprintf(stderr, "FATAL: could not start test server on port %d\n", TEST_PORT);
        return 1;
    }
    usleep(20000);

    UNITY_BEGIN();
    // Phase 1
    RUN_TEST(test_connect_to_server);
    RUN_TEST(test_clear_background_ack);
    RUN_TEST(test_draw_circle_ack);
    RUN_TEST(test_draw_rectangle_ack);
    RUN_TEST(test_draw_text_ack);
    RUN_TEST(test_begin_end_drawing_ack);
    RUN_TEST(test_no_id_no_response);
    RUN_TEST(test_invalid_json_returns_error);
    RUN_TEST(test_multiple_clients_independent);
    RUN_TEST(test_command_reaches_queue);
    // Phase 2
    RUN_TEST(test_line_commands);
    RUN_TEST(test_circle_commands);
    RUN_TEST(test_ellipse_and_ring_commands);
    RUN_TEST(test_rectangle_commands);
    RUN_TEST(test_triangle_commands);
    RUN_TEST(test_polygon_commands);
    RUN_TEST(test_spline_commands);
    RUN_TEST(test_mode_commands);
    // Phase 3 (protocol-level; no raylib context required)
    RUN_TEST(test_sync_cmd_no_optimistic_ack);

    RUN_TEST(test_fire_and_forget_gets_immediate_ack);
    RUN_TEST(test_unload_invalid_handle_no_crash);
    RUN_TEST(test_draw_texture_invalid_handle_no_crash);
    RUN_TEST(test_upload_texture_bad_data_no_crash);
    // Phase 3b: chunked upload (connection-thread commands + sync CommitUpload)
    RUN_TEST(test_begin_upload_returns_upload_id);
    RUN_TEST(test_upload_chunk_ack);
    RUN_TEST(test_abort_upload_ack);
    RUN_TEST(test_list_uploads_shows_pending);
    RUN_TEST(test_commit_upload_is_sync);
    // Phase 1 gap fill: missing window management commands
    RUN_TEST(test_window_management_commands);
    // Phase 4: display lists (handled in connection thread, no render loop needed)
    RUN_TEST(test_display_list_begin_end_ack);
    RUN_TEST(test_display_list_commands_stored);
    RUN_TEST(test_display_list_update_replaces_commands);
    RUN_TEST(test_display_list_clear);
    RUN_TEST(test_display_list_delete);
    RUN_TEST(test_get_display_lists);
    // Phase 5: introspection (sync commands — verify no optimistic ACK)
    RUN_TEST(test_window_query_is_sync);
    RUN_TEST(test_fps_query_is_sync);
    RUN_TEST(test_input_query_is_sync);
    RUN_TEST(test_is_key_query_is_sync);
    RUN_TEST(test_list_handles_is_sync);
    RUN_TEST(test_get_server_info_is_sync);
    RUN_TEST(test_get_texture_info_invalid_is_sync);
    // Phase 6: event streaming (Subscribe / Unsubscribe)
    RUN_TEST(test_subscribe_sends_ack);
    RUN_TEST(test_unsubscribe_sends_ack);
    RUN_TEST(test_subscribe_no_id_no_ack);
    int result = UNITY_END();

    server_stop(&g_server);
    CmdEntry e;
    while (cmdq_pop(&g_queue, &e)) protocol_free(e.parsed);
    cmdq_destroy(&g_queue);
    dl_destroy(&g_dl_registry);
    ur_destroy(&g_ur_registry);
    er_destroy(&g_ev_registry);

    return result;
}
