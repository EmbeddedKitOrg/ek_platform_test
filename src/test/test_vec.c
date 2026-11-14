#include "test.h"
#include "ek/container/vec.h"

EK_LOG_FILE_TAG("test_vec.c");

void vec_test(void)
{
    EK_VEC_IMPLEMENT(float);

    const size_t unused_start = ek_heap_unused();
    ek_vec_t(float) v;
    ek_vec_init(v);

    for (int i = 0; i < 10; i++)
    {
        ek_vec_append(v, (float)(i * 5));
    }

    TEST_ASSERT_EQ(v.amount, 10U, "vec append amount mismatch");
    TEST_ASSERT(v.cap >= 10U, "vec capacity too small");
    TEST_ASSERT(v.items != NULL, "vec items null after append");

    for (uint32_t i = 0; i < 10U; i++)
    {
        TEST_ASSERT(v.items[i] == (float)(i * 5), "vec item value mismatch");
    }

    ek_vec_remove(v, 2);
    TEST_ASSERT_EQ(v.amount, 9U, "vec remove amount mismatch");
    TEST_ASSERT(v.items[0] == 0.0f, "vec remove shift mismatch at 0");
    TEST_ASSERT(v.items[1] == 5.0f, "vec remove shift mismatch at 1");
    TEST_ASSERT(v.items[2] == 15.0f, "vec remove shift mismatch at 2");

    while (v.amount > 4U)
    {
        ek_vec_remove(v, 0);
    }
    TEST_ASSERT_EQ(v.amount, 4U, "vec trim amount mismatch");

    ek_vec_shrink(v);
    TEST_ASSERT_EQ(v.cap, v.amount, "vec shrink cap mismatch");

    ek_vec_clear(v);
    TEST_ASSERT_EQ(v.amount, 0U, "vec clear amount mismatch");
    TEST_ASSERT(v.cap >= 4U, "vec clear should keep capacity");

    for (int i = 0; i < 10; i++)
    {
        ek_vec_append(v, (float)(i * 5));
    }
    TEST_ASSERT_EQ(v.amount, 10U, "vec re-append amount mismatch");

    float removed = v.items[2];
    float last = v.items[9];
    ek_vec_remove_unorder(v, 2);
    TEST_ASSERT_EQ(v.amount, 9U, "vec remove_unorder amount mismatch");
    TEST_ASSERT(v.items[2] == last, "vec remove_unorder swap mismatch");
    TEST_ASSERT(removed == 10.0f, "vec remove_unorder removed value mismatch");

    ek_vec_destroy(v);
    TEST_ASSERT(v.items == NULL, "vec destroy items not null");
    TEST_ASSERT_EQ(v.amount, 0U, "vec destroy amount not zero");
    TEST_ASSERT_EQ(v.cap, 0U, "vec destroy cap not zero");
    TEST_ASSERT_EQ(ek_heap_unused(), unused_start, "vec test leaked heap");

    EK_LOG_INFO("vec test passed, unused heap:%u", (unsigned)ek_heap_unused());
}
