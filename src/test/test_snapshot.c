#include "test.h"
#include "ek/container/snapshot.h"

EK_LOG_FILE_TAG("test_snapshot.c");

EK_SNAPSHOT_DEFINE(static_snapshot, 16);

void snapshot_test(void)
{
    TEST_ASSERT(ek_static_alloc_get_init_error() == 0, "static alloc init failed");
    TEST_ASSERT((uint8_t *)static_snapshot->data == (uint8_t *)static_snapshot + sizeof(*static_snapshot),
                "static snapshot data not inline");
    TEST_ASSERT_EQ(static_snapshot->data_size, 16U, "static snapshot size mismatch");
    TEST_ASSERT_EQ(static_snapshot->unique, 0U, "static snapshot should start empty");

    const size_t unused_before = ek_heap_unused();

    // 静态快照：空读 → 写入 → 读回 → 重复 id 拒绝
    student_t rx;
    TEST_ASSERT(ek_snapshot_get(static_snapshot, &rx) == -ENODATA, "static snapshot should be empty");

    student_t tx = { .name = 7, .score = 7.7f };
    TEST_ASSERT(ek_snapshot_set(static_snapshot, &tx, 100U) == 0, "static snapshot set failed");
    TEST_ASSERT(ek_snapshot_get(static_snapshot, &rx) == 0, "static snapshot get failed");
    TEST_ASSERT(rx.name == tx.name && rx.score == tx.score, "static snapshot data mismatch");
    TEST_ASSERT_EQ(ek_snapshot_get_unique(static_snapshot), 100U, "static snapshot unique mismatch");

    TEST_ASSERT(ek_snapshot_set(static_snapshot, &tx, 100U) == -EINVAL, "duplicate unique should be rejected");

    TEST_ASSERT_EQ(ek_heap_unused(), unused_before, "static snapshot used heap");
    EK_LOG_INFO("static snapshot test passed");

    // 动态快照：控制块与数据单次分配，数据内联
    const size_t unused_before_dyn = ek_heap_unused();
    ek_snapshot_t *snap = ek_snapshot_create(sizeof(student_t));
    TEST_ASSERT(snap != NULL, "creating snapshot fail");
    TEST_ASSERT((uint8_t *)snap->data == (uint8_t *)snap + sizeof(*snap), "dynamic snapshot data not inline");

    TEST_ASSERT(ek_snapshot_get(snap, &rx) == -ENODATA, "dynamic snapshot should be empty");
    TEST_ASSERT_EQ(ek_snapshot_get_unique(snap), 0U, "dynamic snapshot unique should be 0");

    tx.name = 8;
    tx.score = 8.8f;
    TEST_ASSERT(ek_snapshot_set(snap, &tx, 200U) == 0, "dynamic snapshot set failed");
    TEST_ASSERT(ek_snapshot_get(snap, &rx) == 0, "dynamic snapshot get failed");
    TEST_ASSERT(rx.name == tx.name && rx.score == tx.score, "dynamic snapshot data mismatch");

    // 以新 id 覆盖更新
    tx.name = 9;
    TEST_ASSERT(ek_snapshot_set(snap, &tx, 201U) == 0, "snapshot update failed");
    TEST_ASSERT(ek_snapshot_get(snap, &rx) == 0, "snapshot re-get failed");
    TEST_ASSERT_EQ(rx.name, 9, "snapshot update data mismatch");
    TEST_ASSERT_EQ(ek_snapshot_get_unique(snap), 201U, "snapshot update unique mismatch");

    ek_snapshot_destroy(snap);
    TEST_ASSERT_EQ(ek_heap_unused(), unused_before_dyn, "snapshot destroy leaked memory");

    // 参数检查：非法参数返回错误码而非断言
    TEST_ASSERT(ek_snapshot_create(0) == NULL, "create with zero size should fail");
    TEST_ASSERT(ek_snapshot_set(NULL, &tx, 1U) == -EINVAL, "set on null snapshot should fail");
    TEST_ASSERT(ek_snapshot_get(NULL, &rx) == -EINVAL, "get from null snapshot should fail");
    TEST_ASSERT_EQ(ek_snapshot_get_unique(NULL), 0U, "null snapshot unique should be 0");
    ek_snapshot_destroy(NULL);

    EK_LOG_INFO("snapshot test passed, unused heap:%u", (unsigned)ek_heap_unused());
}
