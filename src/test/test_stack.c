#include "test.h"
#include "ek/container/stack.h"

EK_LOG_FILE_TAG("test_stack.c");

EK_DEFINE_STACK(static_stack, int, 4);

void stack_test(void)
{
    TEST_ASSERT(ek_static_alloc_get_init_error() == 0, "static alloc init failed");
    TEST_ASSERT((uint8_t *)static_stack->data == (uint8_t *)static_stack + sizeof(*static_stack),
                "static stack data not inline");
    TEST_ASSERT_EQ(static_stack->cap, 4U, "static stack cap mismatch");

    const size_t unused_before = ek_heap_unused();

    for (int i = 0; i < 4; i++)
    {
        TEST_ASSERT(ek_stack_push(static_stack, &i) == 0, "static stack push failed");
    }

    int extra = 4;
    TEST_ASSERT(ek_stack_push(static_stack, &extra) == -ENOSPC, "static stack should be full");

    for (int i = 0; i < 4; i++)
    {
        int stack_item = -1;
        TEST_ASSERT(ek_stack_pop(static_stack, &stack_item) == 0, "static stack pop failed");
        TEST_ASSERT_EQ(stack_item, (3 - i), "static stack value mismatch");
    }

    TEST_ASSERT_EQ(ek_heap_unused(), unused_before, "static stack used heap");
    EK_LOG_INFO("static stack test passed");

    EK_LOG_INFO("start creating a stack");
    const size_t unused_before_dyn = ek_heap_unused();
    ek_stack_t *stack = ek_stack_create(sizeof(student_t), 5);
    TEST_ASSERT(stack != NULL, "creating stack fail");
    TEST_ASSERT((uint8_t *)stack->data == (uint8_t *)stack + sizeof(*stack), "dynamic stack data not inline");
    EK_LOG_INFO("create successfully");

    student_t items[5];
    for (uint8_t i = 0; i < 5; i++)
    {
        items[i].name = i;
        items[i].score = i * PI;
        TEST_ASSERT(ek_stack_push(stack, &items[i]) == 0, "stack push failed");
        EK_LOG("i:%u push item{name:%u,score:%.2f} sp:%u", i, items[i].name, items[i].score, stack->sp);
    }

    student_t overflow = { .name = 99, .score = 99.0f };
    TEST_ASSERT(ek_stack_push(stack, &overflow) == -ENOSPC, "stack should be full");

    for (uint8_t i = 0; i < 5; i++)
    {
        student_t item = { 0 };
        TEST_ASSERT(ek_stack_pop(stack, &item) == 0, "stack pop failed");
        TEST_ASSERT_EQ(item.name, (uint8_t)(4 - i), "stack pop name mismatch");
        EK_LOG("i:%u pop item{name:%u,score:%.2f} sp:%u", i, item.name, item.score, stack->sp);
    }

    student_t empty_item = { 0 };
    TEST_ASSERT(ek_stack_pop(stack, &empty_item) == -ENODATA, "stack should be empty");

    ek_stack_destroy(stack);
    TEST_ASSERT_EQ(ek_heap_unused(), unused_before_dyn, "stack destroy leaked memory");

    // 参数检查：非法参数返回错误码而非断言
    TEST_ASSERT(ek_stack_create(0, 4) == NULL, "create with zero item_size should fail");
    TEST_ASSERT(ek_stack_create(sizeof(int), 0) == NULL, "create with zero amount should fail");
    TEST_ASSERT(ek_stack_push(NULL, &extra) == -EINVAL, "push to null stack should fail");
    TEST_ASSERT(ek_stack_pop(NULL, &extra) == -EINVAL, "pop from null stack should fail");
    TEST_ASSERT(ek_stack_full(NULL) == false, "null stack full should return false");
    TEST_ASSERT(ek_stack_empty(NULL) == false, "null stack empty should return false");
    ek_stack_destroy(NULL);

    EK_LOG_INFO("stack test passed, unused heap:%u", (unsigned)ek_heap_unused());
}
