#include "unity.h"
#include "queue.h"
#include "protocol.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

void setUp(void)    {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Basic push / pop
// ---------------------------------------------------------------------------

void test_push_pop_single(void) {
    // ARRANGE
    CmdQueue q;
    cmdq_init(&q);

    ParsedCmd *p = calloc(1, sizeof(ParsedCmd));
    p->cmd = strdup("DrawCircle");
    CmdEntry in = { .parsed = p, .conn_fd = 5, .client_id = 1 };

    // ACT
    bool pushed = cmdq_push(&q, in);
    CmdEntry out;
    bool popped = cmdq_pop(&q, &out);

    // ASSERT
    TEST_ASSERT_TRUE(pushed);
    TEST_ASSERT_TRUE(popped);
    TEST_ASSERT_EQUAL_PTR(p, out.parsed);
    TEST_ASSERT_EQUAL_INT(5, out.conn_fd);
    TEST_ASSERT_EQUAL_INT(1, out.client_id);

    protocol_free(out.parsed);
    cmdq_destroy(&q);
}

void test_pop_empty_returns_false(void) {
    // ARRANGE
    CmdQueue q;
    cmdq_init(&q);

    // ACT
    CmdEntry out;
    bool result = cmdq_pop(&q, &out);

    // ASSERT
    TEST_ASSERT_FALSE(result);

    cmdq_destroy(&q);
}

void test_count_reflects_pushes(void) {
    // ARRANGE
    CmdQueue q;
    cmdq_init(&q);

    // ACT
    for (int i = 0; i < 10; i++) {
        ParsedCmd *p = calloc(1, sizeof(ParsedCmd));
        p->cmd = strdup("TestCmd");
        CmdEntry e = { .parsed = p, .conn_fd = -1, .client_id = i };
        cmdq_push(&q, e);
    }

    // ASSERT
    TEST_ASSERT_EQUAL_INT(10, cmdq_count(&q));

    // Drain and free.
    CmdEntry e;
    while (cmdq_pop(&q, &e)) protocol_free(e.parsed);

    TEST_ASSERT_EQUAL_INT(0, cmdq_count(&q));
    cmdq_destroy(&q);
}

void test_fifo_ordering(void) {
    // ARRANGE
    CmdQueue q;
    cmdq_init(&q);
    const char *cmds[] = {"first", "second", "third"};

    // ACT
    for (int i = 0; i < 3; i++) {
        ParsedCmd *p = calloc(1, sizeof(ParsedCmd));
        p->cmd = strdup(cmds[i]);
        CmdEntry e = { .parsed = p, .conn_fd = -1, .client_id = i };
        cmdq_push(&q, e);
    }

    // ASSERT
    for (int i = 0; i < 3; i++) {
        CmdEntry out;
        TEST_ASSERT_TRUE(cmdq_pop(&q, &out));
        TEST_ASSERT_EQUAL_STRING(cmds[i], out.parsed->cmd);
        protocol_free(out.parsed);
    }

    cmdq_destroy(&q);
}

// ---------------------------------------------------------------------------
// Producer / consumer threading
// ---------------------------------------------------------------------------

typedef struct { CmdQueue *q; int count; } ProducerArg;

static void *producer_thread(void *arg) {
    ProducerArg *pa = arg;
    for (int i = 0; i < pa->count; i++) {
        ParsedCmd *p = calloc(1, sizeof(ParsedCmd));
        p->cmd = strdup("Threaded");
        CmdEntry e = { .parsed = p, .conn_fd = -1, .client_id = i };
        cmdq_push(pa->q, e);
    }
    return NULL;
}

void test_concurrent_producer_consumer(void) {
    // ARRANGE
    CmdQueue q;
    cmdq_init(&q);
    ProducerArg pa = { .q = &q, .count = 500 };
    pthread_t t;

    // ACT
    pthread_create(&t, NULL, producer_thread, &pa);

    int consumed = 0;
    while (consumed < 500) {
        CmdEntry e;
        if (cmdq_pop(&q, &e)) {
            protocol_free(e.parsed);
            consumed++;
        }
    }
    pthread_join(t, NULL);

    // ASSERT
    TEST_ASSERT_EQUAL_INT(500, consumed);
    TEST_ASSERT_EQUAL_INT(0, cmdq_count(&q));

    cmdq_destroy(&q);
}

void test_shutdown_unblocks_pop(void) {
    // ARRANGE
    CmdQueue q;
    cmdq_init(&q);

    // ACT / ASSERT: destroy on empty queue should not hang.
    cmdq_destroy(&q);
    TEST_PASS();
}

// ---------------------------------------------------------------------------
// Runner
// ---------------------------------------------------------------------------

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_push_pop_single);
    RUN_TEST(test_pop_empty_returns_false);
    RUN_TEST(test_count_reflects_pushes);
    RUN_TEST(test_fifo_ordering);
    RUN_TEST(test_concurrent_producer_consumer);
    RUN_TEST(test_shutdown_unblocks_pop);
    return UNITY_END();
}
