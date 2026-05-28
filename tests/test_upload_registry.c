// Unit tests for the upload registry module (upload_registry.h/c).
// No raylib or network dependency.

#include "unity.h"
#include "upload_registry.h"
#include "protocol.h"
#include <string.h>
#include <stdlib.h>

void setUp(void)    {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static ParsedCmd *make_cmd(const char *cmd_name, const char *args_json) {
    ParsedCmd *p = calloc(1, sizeof(ParsedCmd));
    p->cmd  = strdup(cmd_name);
    p->id   = strdup("test-id");
    p->args = args_json ? cJSON_Parse(args_json) : NULL;
    return p;
}

#define HANDLE(reg, cmd) ur_handle_cmd((reg), (cmd), -1)

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void test_init_destroy(void) {
    // ARRANGE + ACT
    UploadRegistry reg;
    ur_init(&reg);

    // ASSERT — no active slots
    for (int i = 0; i < UR_MAX_SLOTS; i++)
        TEST_ASSERT_FALSE(reg.slots[i].active);

    ur_destroy(&reg);
}

void test_begin_upload_creates_slot(void) {
    // ARRANGE
    UploadRegistry reg;
    ur_init(&reg);
    ParsedCmd *cmd = make_cmd("BeginUpload",
        "{\"name\":\"sprite\",\"fileType\":\".png\",\"totalBytes\":1024}");

    // ACT
    bool handled = HANDLE(&reg, cmd);

    // ASSERT
    TEST_ASSERT_TRUE(handled);
    bool found = false;
    for (int i = 0; i < UR_MAX_SLOTS; i++)
        if (reg.slots[i].active) { found = true; break; }
    TEST_ASSERT_TRUE(found);

    protocol_free(cmd);
    ur_destroy(&reg);
}

void test_begin_upload_returns_upload_id(void) {
    // ARRANGE
    UploadRegistry reg;
    ur_init(&reg);
    ParsedCmd *cmd = make_cmd("BeginUpload",
        "{\"name\":\"a\",\"fileType\":\".png\",\"totalBytes\":64}");

    // ACT
    HANDLE(&reg, cmd);

    // ASSERT — first slot is active and has a non-empty uploadId
    TEST_ASSERT_TRUE(reg.slots[0].active);
    TEST_ASSERT_GREATER_THAN(0, (int)strlen(reg.slots[0].upload_id));

    protocol_free(cmd);
    ur_destroy(&reg);
}

void test_upload_chunk_appends_bytes(void) {
    // ARRANGE — begin an upload for 3 bytes total
    UploadRegistry reg;
    ur_init(&reg);
    ParsedCmd *begin = make_cmd("BeginUpload",
        "{\"name\":\"t\",\"fileType\":\".png\",\"totalBytes\":3}");
    HANDLE(&reg, begin);

    char upload_id[UR_UPLOAD_ID_LEN];
    strncpy(upload_id, reg.slots[0].upload_id, UR_UPLOAD_ID_LEN);

    // Base64 of "ABC" is "QUJD"
    char args[256];
    snprintf(args, sizeof(args),
             "{\"uploadId\":\"%s\",\"seq\":0,\"data\":\"QUJD\"}", upload_id);
    ParsedCmd *chunk = make_cmd("UploadChunk", args);

    // ACT
    bool handled = HANDLE(&reg, chunk);

    // ASSERT
    TEST_ASSERT_TRUE(handled);
    TEST_ASSERT_EQUAL_size_t(3, reg.slots[0].bytes_received);
    TEST_ASSERT_EQUAL_INT8('A', (char)reg.slots[0].buf[0]);
    TEST_ASSERT_EQUAL_INT8('B', (char)reg.slots[0].buf[1]);
    TEST_ASSERT_EQUAL_INT8('C', (char)reg.slots[0].buf[2]);

    protocol_free(begin);
    protocol_free(chunk);
    ur_destroy(&reg);
}

void test_out_of_order_chunk_rejected(void) {
    // ARRANGE
    UploadRegistry reg;
    ur_init(&reg);
    ParsedCmd *begin = make_cmd("BeginUpload",
        "{\"name\":\"t\",\"fileType\":\".png\",\"totalBytes\":6}");
    HANDLE(&reg, begin);

    char upload_id[UR_UPLOAD_ID_LEN];
    strncpy(upload_id, reg.slots[0].upload_id, UR_UPLOAD_ID_LEN);

    char args[256];
    // Send seq=1 before seq=0.
    snprintf(args, sizeof(args),
             "{\"uploadId\":\"%s\",\"seq\":1,\"data\":\"QUJD\"}", upload_id);
    ParsedCmd *bad_chunk = make_cmd("UploadChunk", args);

    // ACT
    bool handled = HANDLE(&reg, bad_chunk);

    // ASSERT — consumed (error sent), but no bytes recorded
    TEST_ASSERT_TRUE(handled);
    TEST_ASSERT_EQUAL_size_t(0, reg.slots[0].bytes_received);

    protocol_free(begin);
    protocol_free(bad_chunk);
    ur_destroy(&reg);
}

void test_abort_upload_frees_slot(void) {
    // ARRANGE
    UploadRegistry reg;
    ur_init(&reg);
    ParsedCmd *begin = make_cmd("BeginUpload",
        "{\"name\":\"t\",\"fileType\":\".png\",\"totalBytes\":64}");
    HANDLE(&reg, begin);

    char upload_id[UR_UPLOAD_ID_LEN];
    strncpy(upload_id, reg.slots[0].upload_id, UR_UPLOAD_ID_LEN);

    char args[128];
    snprintf(args, sizeof(args), "{\"uploadId\":\"%s\"}", upload_id);
    ParsedCmd *abort_cmd = make_cmd("AbortUpload", args);

    // ACT
    bool handled = HANDLE(&reg, abort_cmd);

    // ASSERT
    TEST_ASSERT_TRUE(handled);
    TEST_ASSERT_FALSE(reg.slots[0].active);

    protocol_free(begin);
    protocol_free(abort_cmd);
    ur_destroy(&reg);
}

void test_list_uploads_shows_active(void) {
    // ARRANGE
    UploadRegistry reg;
    ur_init(&reg);
    ParsedCmd *begin = make_cmd("BeginUpload",
        "{\"name\":\"myfile\",\"fileType\":\".png\",\"totalBytes\":128}");
    HANDLE(&reg, begin);

    ParsedCmd *list = make_cmd("ListUploads", NULL);

    // ACT
    bool handled = HANDLE(&reg, list);

    // ASSERT — command consumed; active slot present
    TEST_ASSERT_TRUE(handled);
    int count = 0;
    for (int i = 0; i < UR_MAX_SLOTS; i++)
        if (reg.slots[i].active) count++;
    TEST_ASSERT_EQUAL_INT(1, count);

    protocol_free(begin);
    protocol_free(list);
    ur_destroy(&reg);
}

void test_commit_take_returns_data(void) {
    // ARRANGE — begin + single chunk of "ABC" (3 bytes total)
    UploadRegistry reg;
    ur_init(&reg);
    ParsedCmd *begin = make_cmd("BeginUpload",
        "{\"name\":\"t\",\"fileType\":\".png\",\"totalBytes\":3}");
    HANDLE(&reg, begin);

    char upload_id[UR_UPLOAD_ID_LEN];
    strncpy(upload_id, reg.slots[0].upload_id, UR_UPLOAD_ID_LEN);

    char args[256];
    snprintf(args, sizeof(args),
             "{\"uploadId\":\"%s\",\"seq\":0,\"data\":\"QUJD\"}", upload_id);
    ParsedCmd *chunk = make_cmd("UploadChunk", args);
    HANDLE(&reg, chunk);

    // ACT
    UploadCommitInfo info;
    bool ok = ur_commit_take(&reg, upload_id, &info);

    // ASSERT
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_size_t(3, info.total_bytes);
    TEST_ASSERT_EQUAL_INT8('A', (char)info.buf[0]);
    TEST_ASSERT_EQUAL_INT8('B', (char)info.buf[1]);
    TEST_ASSERT_EQUAL_INT8('C', (char)info.buf[2]);
    // Slot should be gone.
    TEST_ASSERT_FALSE(reg.slots[0].active);

    free(info.buf);
    protocol_free(begin);
    protocol_free(chunk);
    ur_destroy(&reg);
}

void test_commit_take_fails_if_incomplete(void) {
    // ARRANGE — begin 6 bytes but only send 3
    UploadRegistry reg;
    ur_init(&reg);
    ParsedCmd *begin = make_cmd("BeginUpload",
        "{\"name\":\"t\",\"fileType\":\".png\",\"totalBytes\":6}");
    HANDLE(&reg, begin);

    char upload_id[UR_UPLOAD_ID_LEN];
    strncpy(upload_id, reg.slots[0].upload_id, UR_UPLOAD_ID_LEN);

    char args[256];
    snprintf(args, sizeof(args),
             "{\"uploadId\":\"%s\",\"seq\":0,\"data\":\"QUJD\"}", upload_id);
    ParsedCmd *chunk = make_cmd("UploadChunk", args);
    HANDLE(&reg, chunk);

    // ACT — try to commit before all bytes received
    UploadCommitInfo info;
    bool ok = ur_commit_take(&reg, upload_id, &info);

    // ASSERT
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_TRUE(reg.slots[0].active); // still there

    protocol_free(begin);
    protocol_free(chunk);
    ur_destroy(&reg);
}

void test_commit_does_not_handle_cmd(void) {
    // ARRANGE — CommitUpload must go to the queue, not be handled here
    UploadRegistry reg;
    ur_init(&reg);
    ParsedCmd *commit = make_cmd("CommitUpload",
        "{\"uploadId\":\"u-1\",\"type\":\"texture\"}");

    // ACT
    bool handled = HANDLE(&reg, commit);

    // ASSERT
    TEST_ASSERT_FALSE(handled);

    protocol_free(commit);
    ur_destroy(&reg);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_destroy);
    RUN_TEST(test_begin_upload_creates_slot);
    RUN_TEST(test_begin_upload_returns_upload_id);
    RUN_TEST(test_upload_chunk_appends_bytes);
    RUN_TEST(test_out_of_order_chunk_rejected);
    RUN_TEST(test_abort_upload_frees_slot);
    RUN_TEST(test_list_uploads_shows_active);
    RUN_TEST(test_commit_take_returns_data);
    RUN_TEST(test_commit_take_fails_if_incomplete);
    RUN_TEST(test_commit_does_not_handle_cmd);
    return UNITY_END();
}
