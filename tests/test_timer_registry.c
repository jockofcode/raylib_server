#include "unity.h"
#include "timer_registry.h"
#include <string.h>

static TimerRegistry g_reg;

void setUp(void)    { timer_registry_init(&g_reg); }
void tearDown(void) { timer_registry_destroy(&g_reg); }

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

typedef struct {
    int   count;
    char  last_id[TIMER_ID_LEN];
    char  last_name[64];
} FiredLog;

static void log_fired(const char *id, const char *name, void *userdata) {
    FiredLog *log = userdata;
    log->count++;
    strncpy(log->last_id,   id,   sizeof(log->last_id)   - 1);
    strncpy(log->last_name, name, sizeof(log->last_name) - 1);
}

// ---------------------------------------------------------------------------
// timer_create / timer_delete
// ---------------------------------------------------------------------------

void test_create_returns_id(void) {
    char id[TIMER_ID_LEN];
    bool ok = timer_create(&g_reg, "test", 1.0, true, id);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT8('t', (unsigned char)id[0]);
    TEST_ASSERT_GREATER_THAN(1, (int)strlen(id));
}

void test_create_multiple_unique_ids(void) {
    char id1[TIMER_ID_LEN], id2[TIMER_ID_LEN];
    timer_create(&g_reg, "a", 1.0, true, id1);
    timer_create(&g_reg, "b", 1.0, true, id2);
    TEST_ASSERT_NOT_EQUAL(0, strcmp(id1, id2));
}

void test_delete_existing_timer(void) {
    char id[TIMER_ID_LEN];
    timer_create(&g_reg, "x", 0.5, true, id);
    bool ok = timer_delete(&g_reg, id);
    TEST_ASSERT_TRUE(ok);
}

void test_delete_unknown_id_returns_false(void) {
    bool ok = timer_delete(&g_reg, "t000000000000");
    TEST_ASSERT_FALSE(ok);
}

void test_delete_null_id_returns_false(void) {
    bool ok = timer_delete(&g_reg, NULL);
    TEST_ASSERT_FALSE(ok);
}

// ---------------------------------------------------------------------------
// timer_tick — repeating
// ---------------------------------------------------------------------------

void test_repeating_fires_at_interval(void) {
    char id[TIMER_ID_LEN];
    timer_create(&g_reg, "rep", 1.0, true, id);

    FiredLog log = {0};
    // Half-tick: should not fire.
    timer_tick(&g_reg, 0.5, log_fired, &log);
    TEST_ASSERT_EQUAL_INT(0, log.count);

    // Full interval: should fire once.
    timer_tick(&g_reg, 0.5, log_fired, &log);
    TEST_ASSERT_EQUAL_INT(1, log.count);
    TEST_ASSERT_EQUAL_STRING(id, log.last_id);
    TEST_ASSERT_EQUAL_STRING("rep", log.last_name);
}

void test_repeating_fires_multiple_times(void) {
    char id[TIMER_ID_LEN];
    timer_create(&g_reg, "fast", 0.1, true, id);

    FiredLog log = {0};
    // 5 intervals at once.
    timer_tick(&g_reg, 0.5, log_fired, &log);
    TEST_ASSERT_EQUAL_INT(5, log.count);
}

void test_repeating_stays_active_after_fire(void) {
    char id[TIMER_ID_LEN];
    timer_create(&g_reg, "r", 1.0, true, id);

    FiredLog log = {0};
    timer_tick(&g_reg, 1.0, log_fired, &log);
    TEST_ASSERT_EQUAL_INT(1, log.count);

    // Tick again — should fire again.
    timer_tick(&g_reg, 1.0, log_fired, &log);
    TEST_ASSERT_EQUAL_INT(2, log.count);
}

// ---------------------------------------------------------------------------
// timer_tick — one-shot
// ---------------------------------------------------------------------------

void test_oneshot_fires_once(void) {
    char id[TIMER_ID_LEN];
    timer_create(&g_reg, "once", 0.5, false, id);

    FiredLog log = {0};
    timer_tick(&g_reg, 1.0, log_fired, &log);
    TEST_ASSERT_EQUAL_INT(1, log.count);

    // Timer is removed; second tick should not fire.
    timer_tick(&g_reg, 1.0, log_fired, &log);
    TEST_ASSERT_EQUAL_INT(1, log.count);
}

void test_oneshot_not_yet_due(void) {
    char id[TIMER_ID_LEN];
    timer_create(&g_reg, "later", 2.0, false, id);

    FiredLog log = {0};
    timer_tick(&g_reg, 1.0, log_fired, &log);
    TEST_ASSERT_EQUAL_INT(0, log.count);
}

// ---------------------------------------------------------------------------
// timer_list_json
// ---------------------------------------------------------------------------

void test_list_json_empty(void) {
    cJSON *arr = timer_list_json(&g_reg);
    TEST_ASSERT_NOT_NULL(arr);
    TEST_ASSERT_TRUE(cJSON_IsArray(arr));
    TEST_ASSERT_EQUAL_INT(0, cJSON_GetArraySize(arr));
    cJSON_Delete(arr);
}

void test_list_json_has_entry(void) {
    char id[TIMER_ID_LEN];
    timer_create(&g_reg, "listed", 0.25, true, id);

    cJSON *arr = timer_list_json(&g_reg);
    TEST_ASSERT_EQUAL_INT(1, cJSON_GetArraySize(arr));

    cJSON *obj = cJSON_GetArrayItem(arr, 0);
    TEST_ASSERT_NOT_NULL(obj);
    cJSON *tid  = cJSON_GetObjectItemCaseSensitive(obj, "timerId");
    cJSON *tnam = cJSON_GetObjectItemCaseSensitive(obj, "name");
    cJSON *tint = cJSON_GetObjectItemCaseSensitive(obj, "interval");
    TEST_ASSERT_NOT_NULL(tid);
    TEST_ASSERT_NOT_NULL(tnam);
    TEST_ASSERT_NOT_NULL(tint);
    TEST_ASSERT_EQUAL_STRING(id, tid->valuestring);
    TEST_ASSERT_EQUAL_STRING("listed", tnam->valuestring);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.25f, (float)tint->valuedouble);
    cJSON_Delete(arr);
}

void test_list_json_removed_after_delete(void) {
    char id[TIMER_ID_LEN];
    timer_create(&g_reg, "tmp", 1.0, true, id);
    timer_delete(&g_reg, id);

    cJSON *arr = timer_list_json(&g_reg);
    TEST_ASSERT_EQUAL_INT(0, cJSON_GetArraySize(arr));
    cJSON_Delete(arr);
}

// ---------------------------------------------------------------------------
// Runner
// ---------------------------------------------------------------------------

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_create_returns_id);
    RUN_TEST(test_create_multiple_unique_ids);
    RUN_TEST(test_delete_existing_timer);
    RUN_TEST(test_delete_unknown_id_returns_false);
    RUN_TEST(test_delete_null_id_returns_false);
    RUN_TEST(test_repeating_fires_at_interval);
    RUN_TEST(test_repeating_fires_multiple_times);
    RUN_TEST(test_repeating_stays_active_after_fire);
    RUN_TEST(test_oneshot_fires_once);
    RUN_TEST(test_oneshot_not_yet_due);
    RUN_TEST(test_list_json_empty);
    RUN_TEST(test_list_json_has_entry);
    RUN_TEST(test_list_json_removed_after_delete);
    return UNITY_END();
}
