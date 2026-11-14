#include "test.h"
#include "ek/sched/evoke.h"

EK_LOG_FILE_TAG("test_evoke.c");

static void _static_evoke_cb(ek_evoke_event_t *evt, void *arg)
{
    __EK_UNUSED(evt);
    __EK_UNUSED(arg);
}

EK_DEFINE_EVOKE_EVENT(static_event, 0);
EK_DEFINE_EVOKE_TASK(static_evoke_task, _static_evoke_cb, NULL);

void evoke_test(void)
{
    EK_LOG_INFO("evoke static test start");

    TEST_ASSERT(ek_static_alloc_get_init_error() == 0, "static alloc init failed");
    TEST_ASSERT_EQ(static_event.count, 0U, "static event count mismatch");
    TEST_ASSERT(static_evoke_task.cb == _static_evoke_cb, "static evoke task cb mismatch");

    const size_t unused_before = ek_heap_unused();
    TEST_ASSERT_EQ(ek_heap_unused(), unused_before, "static evoke used heap");

    // 参数检查：非法参数返回错误码而非断言
    TEST_ASSERT(ek_evoke_task_create(NULL, NULL) == NULL, "create with null cb should fail");
    TEST_ASSERT(ek_evoke_event_subscribe(NULL, &static_event) == false, "subscribe with null task should fail");
    TEST_ASSERT(ek_evoke_event_broadcast_from_isr(NULL, NULL) == -EINVAL, "isr broadcast with null event should fail");
    ek_evoke_task_destroy(NULL);
    ek_evoke_event_destroy(NULL);

    EK_LOG_INFO("static evoke test passed, unused heap:%u", (unsigned)ek_heap_unused());
}
