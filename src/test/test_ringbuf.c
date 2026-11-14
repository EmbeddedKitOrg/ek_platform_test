#include "test.h"
#include "ek/container/ringbuf.h"

EK_LOG_FILE_TAG("test_ringbuf.c");

EK_DEFINE_RINGBUF(static_ringbuf, int, 4);
EK_DEFINE_RINGBUF_SPSC(static_spsc, int, 5);

void ringbuf_test(void)
{
    TEST_ASSERT(ek_static_alloc_get_init_error() == 0, "static alloc init failed");
    TEST_ASSERT((uint8_t *)static_ringbuf->data == (uint8_t *)static_ringbuf + sizeof(*static_ringbuf),
                "static ringbuf data not inline");
    TEST_ASSERT_EQ(static_ringbuf->cap, 4U, "static ringbuf cap mismatch");
    TEST_ASSERT((uint8_t *)static_spsc->data == (uint8_t *)static_spsc + sizeof(*static_spsc),
                "static spsc data not inline");
    TEST_ASSERT_EQ(static_spsc->cap, 5U, "static spsc cap mismatch");

    const size_t unused_before = ek_heap_unused();

    for (int i = 0; i < 4; i++)
    {
        TEST_ASSERT(ek_ringbuf_write(static_ringbuf, &i) == 0, "static ringbuf write failed");
        TEST_ASSERT(ek_ringbuf_write_spsc(static_spsc, &i) == 0, "static spsc write failed");
    }

    int extra = 4;
    TEST_ASSERT(ek_ringbuf_write(static_ringbuf, &extra) == -ENOSPC, "static ringbuf should be full");
    TEST_ASSERT(ek_ringbuf_write_spsc(static_spsc, &extra) == -ENOSPC, "static spsc should be full");

    for (int i = 0; i < 4; i++)
    {
        int ring_item = -1;
        int spsc_item = -1;

        TEST_ASSERT(ek_ringbuf_read(static_ringbuf, &ring_item) == 0, "static ringbuf read failed");
        TEST_ASSERT(ek_ringbuf_read_spsc(static_spsc, &spsc_item) == 0, "static spsc read failed");
        TEST_ASSERT_EQ(ring_item, i, "static ringbuf value mismatch");
        TEST_ASSERT_EQ(spsc_item, i, "static spsc value mismatch");
    }

    TEST_ASSERT_EQ(ek_heap_unused(), unused_before, "static ringbuf used heap");
    EK_LOG_INFO("static ringbuf test passed");

    EK_LOG_INFO("start creating a rb");
    const size_t unused_before_dyn = ek_heap_unused();
    ek_ringbuf_t *rb = ek_ringbuf_create(sizeof(student_t), 5);
    TEST_ASSERT(rb != NULL, "creating rb fail");
    TEST_ASSERT((uint8_t *)rb->data == (uint8_t *)rb + sizeof(*rb), "dynamic ringbuf data not inline");
    EK_LOG_INFO("create successfully");

    student_t items[5];
    for (uint8_t i = 0; i < 5; i++)
    {
        items[i].name = i;
        items[i].score = i * PI;
        TEST_ASSERT(ek_ringbuf_write(rb, &items[i]) == 0, "ringbuf write failed");
        EK_LOG_INFO("i:%u push item{name:%u,score:%.2f} ridx:%u widx:%u",
                    i,
                    items[i].name,
                    items[i].score,
                    rb->read_idx,
                    rb->write_idx);
    }

    student_t overflow = { .name = 99, .score = 99.0f };
    TEST_ASSERT(ek_ringbuf_write(rb, &overflow) == -ENOSPC, "ringbuf should be full");

    for (uint8_t i = 0; i < 5; i++)
    {
        student_t item = { 0 };
        TEST_ASSERT(ek_ringbuf_read(rb, &item) == 0, "ringbuf read failed");
        TEST_ASSERT_EQ(item.name, i, "ringbuf read name mismatch");
        EK_LOG_INFO(
            "i:%u pop item{name:%u,score:%.2f} ridx:%u widx:%u", i, item.name, item.score, rb->read_idx, rb->write_idx);
    }

    student_t empty_item = { 0 };
    TEST_ASSERT(ek_ringbuf_read(rb, &empty_item) == -ENODATA, "ringbuf should be empty");

    ek_ringbuf_destroy(rb);
    TEST_ASSERT_EQ(ek_heap_unused(), unused_before_dyn, "ringbuf destroy leaked memory");

    // 参数检查：非法参数返回错误码而非断言
    TEST_ASSERT(ek_ringbuf_create(sizeof(int), 0) == NULL, "create with zero amount should fail");
    TEST_ASSERT(ek_ringbuf_create_spsc(sizeof(int), 1) == NULL, "spsc create with amount<=1 should fail");
    TEST_ASSERT(ek_ringbuf_write(NULL, &extra) == -EINVAL, "write to null ringbuf should fail");
    TEST_ASSERT(ek_ringbuf_read(NULL, &extra) == -EINVAL, "read from null ringbuf should fail");
    TEST_ASSERT(ek_ringbuf_peek(NULL, &extra) == -EINVAL, "peek null ringbuf should fail");
    TEST_ASSERT(ek_ringbuf_full(NULL) == false, "null ringbuf full should return false");
    TEST_ASSERT(ek_ringbuf_empty(NULL) == false, "null ringbuf empty should return false");
    TEST_ASSERT(ek_ringbuf_write_spsc(NULL, &extra) == -EINVAL, "spsc write to null should fail");
    ek_ringbuf_destroy(NULL);
    ek_ringbuf_destroy_spsc(NULL);

    EK_LOG_INFO("ringbuf test passed, unused heap:%u", (unsigned)ek_heap_unused());
}
