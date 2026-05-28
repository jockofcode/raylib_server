#include "unity.h"
#include "handle_registry.h"
#include <stdint.h>

void setUp(void)    {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Allocation and lookup
// ---------------------------------------------------------------------------

void test_alloc_returns_nonzero_handle(void) {
    // ARRANGE
    HandleRegistry r;
    handle_registry_init(&r);

    // ACT
    int id = handle_alloc(&r, HANDLE_TEXTURE, (void *)0x1234);

    // ASSERT
    TEST_ASSERT_NOT_EQUAL(0, id);
}

void test_alloc_lookup_roundtrip(void) {
    // ARRANGE
    HandleRegistry r;
    handle_registry_init(&r);
    void *fake_ptr = (void *)(uintptr_t)0xDEADBEEF;

    // ACT
    int   id  = handle_alloc(&r, HANDLE_TEXTURE, fake_ptr);
    void *got = handle_get(&r, id, HANDLE_TEXTURE);

    // ASSERT
    TEST_ASSERT_EQUAL_PTR(fake_ptr, got);
}

void test_alloc_multiple_handles_unique(void) {
    // ARRANGE
    HandleRegistry r;
    handle_registry_init(&r);

    // ACT
    int a = handle_alloc(&r, HANDLE_TEXTURE, (void *)1);
    int b = handle_alloc(&r, HANDLE_FONT,    (void *)2);
    int c = handle_alloc(&r, HANDLE_SOUND,   (void *)3);

    // ASSERT
    TEST_ASSERT_NOT_EQUAL(a, b);
    TEST_ASSERT_NOT_EQUAL(b, c);
    TEST_ASSERT_NOT_EQUAL(a, c);
}

void test_get_wrong_kind_returns_null(void) {
    // ARRANGE
    HandleRegistry r;
    handle_registry_init(&r);
    int id = handle_alloc(&r, HANDLE_TEXTURE, (void *)1);

    // ACT
    void *got = handle_get(&r, id, HANDLE_FONT);

    // ASSERT
    TEST_ASSERT_NULL(got);
}

void test_get_invalid_id_returns_null(void) {
    // ARRANGE
    HandleRegistry r;
    handle_registry_init(&r);

    // ACT / ASSERT
    TEST_ASSERT_NULL(handle_get(&r, 0,           HANDLE_TEXTURE));
    TEST_ASSERT_NULL(handle_get(&r, HANDLE_MAX + 1, HANDLE_TEXTURE));
    TEST_ASSERT_NULL(handle_get(&r, -1,          HANDLE_TEXTURE));
}

// ---------------------------------------------------------------------------
// Free
// ---------------------------------------------------------------------------

void test_free_makes_handle_invalid(void) {
    // ARRANGE
    HandleRegistry r;
    handle_registry_init(&r);
    int id = handle_alloc(&r, HANDLE_TEXTURE, (void *)1);

    // ACT
    bool freed = handle_free(&r, id);

    // ASSERT
    TEST_ASSERT_TRUE(freed);
    TEST_ASSERT_FALSE(handle_valid(&r, id));
    TEST_ASSERT_NULL(handle_get(&r, id, HANDLE_TEXTURE));
}

void test_double_free_returns_false(void) {
    // ARRANGE
    HandleRegistry r;
    handle_registry_init(&r);
    int id = handle_alloc(&r, HANDLE_TEXTURE, (void *)1);
    handle_free(&r, id);

    // ACT
    bool second_free = handle_free(&r, id);

    // ASSERT
    TEST_ASSERT_FALSE(second_free);
}

void test_free_invalid_id_returns_false(void) {
    // ARRANGE
    HandleRegistry r;
    handle_registry_init(&r);

    // ACT / ASSERT
    TEST_ASSERT_FALSE(handle_free(&r, 0));
    TEST_ASSERT_FALSE(handle_free(&r, HANDLE_MAX + 1));
}

// ---------------------------------------------------------------------------
// Slot recycling
// ---------------------------------------------------------------------------

void test_freed_slot_can_be_reallocated(void) {
    // ARRANGE
    HandleRegistry r;
    handle_registry_init(&r);
    int first = handle_alloc(&r, HANDLE_TEXTURE, (void *)1);
    handle_free(&r, first);

    // ACT
    int second = handle_alloc(&r, HANDLE_FONT, (void *)2);

    // ASSERT — the registry should reuse the freed slot
    TEST_ASSERT_NOT_EQUAL(0, second);
    TEST_ASSERT_EQUAL_PTR((void *)2, handle_get(&r, second, HANDLE_FONT));
}

// ---------------------------------------------------------------------------
// handle_valid and handle_kind
// ---------------------------------------------------------------------------

void test_valid_and_kind(void) {
    // ARRANGE
    HandleRegistry r;
    handle_registry_init(&r);
    int id = handle_alloc(&r, HANDLE_SHADER, (void *)42);

    // ACT / ASSERT
    TEST_ASSERT_TRUE(handle_valid(&r, id));
    TEST_ASSERT_EQUAL_INT(HANDLE_SHADER, handle_kind(&r, id));

    handle_free(&r, id);
    TEST_ASSERT_FALSE(handle_valid(&r, id));
    TEST_ASSERT_EQUAL_INT(HANDLE_NONE, handle_kind(&r, id));
}

// ---------------------------------------------------------------------------
// handle_iterate
// ---------------------------------------------------------------------------

static int g_iterate_count;
static int g_iterate_sum;

static void count_cb(int id, void *ptr, void *userdata) {
    (void)ptr; (void)userdata;
    g_iterate_count++;
    g_iterate_sum += id;
}

void test_iterate_visits_matching_kind(void) {
    // ARRANGE
    HandleRegistry r;
    handle_registry_init(&r);
    int a = handle_alloc(&r, HANDLE_TEXTURE, (void *)1);
    int b = handle_alloc(&r, HANDLE_TEXTURE, (void *)2);
    handle_alloc(&r, HANDLE_FONT, (void *)3);  // different kind
    g_iterate_count = 0;
    g_iterate_sum   = 0;

    // ACT
    handle_iterate(&r, HANDLE_TEXTURE, count_cb, NULL);

    // ASSERT — only the two TEXTURE handles are visited
    TEST_ASSERT_EQUAL_INT(2, g_iterate_count);
    TEST_ASSERT_EQUAL_INT(a + b, g_iterate_sum);
}

void test_iterate_skips_freed_slots(void) {
    // ARRANGE
    HandleRegistry r;
    handle_registry_init(&r);
    int a = handle_alloc(&r, HANDLE_TEXTURE, (void *)1);
    int b = handle_alloc(&r, HANDLE_TEXTURE, (void *)2);
    handle_free(&r, a);
    g_iterate_count = 0;

    // ACT
    handle_iterate(&r, HANDLE_TEXTURE, count_cb, NULL);

    // ASSERT — only b is visited
    TEST_ASSERT_EQUAL_INT(1, g_iterate_count);
    (void)b;
}

void test_iterate_empty_registry(void) {
    // ARRANGE
    HandleRegistry r;
    handle_registry_init(&r);
    g_iterate_count = 0;

    // ACT
    handle_iterate(&r, HANDLE_TEXTURE, count_cb, NULL);

    // ASSERT
    TEST_ASSERT_EQUAL_INT(0, g_iterate_count);
}

static int g_ptr_sum;
static void sum_ptr_cb(int id, void *ptr, void *userdata) {
    (void)id; (void)userdata;
    g_ptr_sum += (int)(uintptr_t)ptr;
}

void test_iterate_passes_correct_ptr(void) {
    // ARRANGE
    HandleRegistry r;
    handle_registry_init(&r);
    handle_alloc(&r, HANDLE_SOUND, (void *)(uintptr_t)10);
    handle_alloc(&r, HANDLE_SOUND, (void *)(uintptr_t)20);
    handle_alloc(&r, HANDLE_SOUND, (void *)(uintptr_t)30);
    g_ptr_sum = 0;

    // ACT
    handle_iterate(&r, HANDLE_SOUND, sum_ptr_cb, NULL);

    // ASSERT
    TEST_ASSERT_EQUAL_INT(60, g_ptr_sum);
}

// ---------------------------------------------------------------------------
// Runner
// ---------------------------------------------------------------------------

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_alloc_returns_nonzero_handle);
    RUN_TEST(test_alloc_lookup_roundtrip);
    RUN_TEST(test_alloc_multiple_handles_unique);
    RUN_TEST(test_get_wrong_kind_returns_null);
    RUN_TEST(test_get_invalid_id_returns_null);
    RUN_TEST(test_free_makes_handle_invalid);
    RUN_TEST(test_double_free_returns_false);
    RUN_TEST(test_free_invalid_id_returns_false);
    RUN_TEST(test_freed_slot_can_be_reallocated);
    RUN_TEST(test_valid_and_kind);
    RUN_TEST(test_iterate_visits_matching_kind);
    RUN_TEST(test_iterate_skips_freed_slots);
    RUN_TEST(test_iterate_empty_registry);
    RUN_TEST(test_iterate_passes_correct_ptr);
    return UNITY_END();
}
