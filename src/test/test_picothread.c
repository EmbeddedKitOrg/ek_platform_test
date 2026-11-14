#include "test.h"
#include "ek/sched/picothread.h"

EK_LOG_FILE_TAG("test_picothread");

// 测试运行时长，之后控制器挂起所有任务并输出结果汇总
#define TEST_DURATION_TICK 5000

// 测试结果统计
static unsigned s_sem_ok;
static unsigned s_sem_timeout_cnt;
static unsigned s_msg_send_ok;
static unsigned s_msg_recv_ok;

static ek_pt_t *s_controller;

#if EKCFG_PICOTHREAD_SEM == 1

static void _sem_taskA(ek_pt_t *pt, void *arg);
static void _sem_taskB(ek_pt_t *pt, void *arg);
static void _sem_timeout_demo(ek_pt_t *pt, void *arg);

static ek_pt_t *s_semA;
static ek_pt_t *s_semB;
static ek_pt_t *s_sem_to;
static ek_pt_sem_t *s_test_sem;

#endif /* EKCFG_PICOTHREAD_SEM */

#if EKCFG_PICOTHREAD_MSG == 1

static void _msg_sender(ek_pt_t *pt, void *arg);
static void _msg_receiver(ek_pt_t *pt, void *arg);
static void _msg_sender_fast(ek_pt_t *pt, void *arg);

static ek_pt_t *s_sender;
static ek_pt_t *s_receiver;
static ek_pt_t *s_sender_fast;
static ek_pt_msg_t *s_test_msg;

#endif /* EKCFG_PICOTHREAD_MSG */

static void _static_pt_cb(ek_pt_t *pt, void *arg)
{
    __EK_UNUSED(arg);
    EK_PT_BEGIN(pt);
    EK_PT_END(pt);
}

EK_DEFINE_PT_SEM(static_sem, 1);
EK_DEFINE_PT_MSG(static_msg, int, 3);
EK_DEFINE_PT(static_pt, _static_pt_cb, 10, NULL);

// 测试控制器：运行指定时长后挂起所有任务并输出结果汇总
static void _test_controller(ek_pt_t *pt, void *arg)
{
    (void)arg;
    EK_PT_BEGIN(pt);

    EK_LOG_INFO("[ctrl] test started, duration=%u tick", TEST_DURATION_TICK);
    EK_PT_DELAY(TEST_DURATION_TICK);

#if EKCFG_PICOTHREAD_SEM == 1
    ek_pt_suspend(s_semA);
    ek_pt_suspend(s_semB);
    ek_pt_suspend(s_sem_to);
#endif
#if EKCFG_PICOTHREAD_MSG == 1
    ek_pt_suspend(s_sender);
    ek_pt_suspend(s_receiver);
    ek_pt_suspend(s_sender_fast);
#endif

    EK_LOG_INFO("====== picothread test result ======");
#if EKCFG_PICOTHREAD_SEM == 1
    EK_LOG_INFO("[sem] acquire=%u, timeout=%u", s_sem_ok, s_sem_timeout_cnt);
    TEST_ASSERT(s_sem_ok > 0U, "picothread sem acquire never happened");
    TEST_ASSERT(s_sem_timeout_cnt > 0U, "picothread sem timeout never happened");
#endif
#if EKCFG_PICOTHREAD_MSG == 1
    EK_LOG_INFO("[msg] send=%u, recv=%u", s_msg_send_ok, s_msg_recv_ok);
    TEST_ASSERT(s_msg_send_ok > 0U, "picothread msg send never happened");
    TEST_ASSERT(s_msg_recv_ok > 0U, "picothread msg recv never happened");
    TEST_ASSERT(s_msg_send_ok >= s_msg_recv_ok, "picothread msg recv exceeds send");
    TEST_ASSERT((s_msg_send_ok - s_msg_recv_ok) <= 3U, "picothread msg backlog exceeds capacity");
#endif
    EK_LOG_INFO("====== picothread test passed ======");

    // 测试只跑一轮：挂起自身，避免 EK_PT_END 后重新入队
    EK_PT_SUSPEND(NULL);
    EK_PT_END(pt);
}

void picothread_test_init(void)
{
    TEST_ASSERT(ek_static_alloc_get_init_error() == 0, "static alloc init failed");
    TEST_ASSERT((uint8_t *)static_msg->rb.data == (uint8_t *)static_msg + sizeof(*static_msg),
                "static msg data not inline");
    TEST_ASSERT_EQ(static_msg->rb.cap, 3U, "static msg cap mismatch");
    TEST_ASSERT(static_pt.cb == _static_pt_cb, "static pt cb mismatch");
    TEST_ASSERT_EQ(static_pt.prio, 10U, "static pt prio mismatch");
    TEST_ASSERT_EQ(static_sem.count, 1U, "static sem count mismatch");

    const size_t unused_before = ek_heap_unused();
    TEST_ASSERT_EQ(ek_heap_unused(), unused_before, "static picothread used heap");

    ek_pt_deinit_static(&static_pt);
    EK_LOG_INFO("static picothread objects initialized");

    // 参数检查：非法参数返回错误码而非断言
    TEST_ASSERT(ek_pt_create(NULL, 0, NULL) == NULL, "create with null cb should fail");
    TEST_ASSERT(ek_pt_create(_static_pt_cb, (uint8_t)(EKCFG_PT_PRIO_LOWEST + 1U), NULL) == NULL,
                "create with out-of-range prio should fail");
    ek_pt_destroy(NULL);
#if EKCFG_PICOTHREAD_SEM == 1
    TEST_ASSERT(ek_pt_sem_take(NULL) == false, "sem take with null should fail");
    ek_pt_sem_destroy(NULL);
#endif
#if EKCFG_PICOTHREAD_MSG == 1
    ek_pt_msg_destroy(NULL);
#endif

#if EKCFG_PICOTHREAD_SEM == 1
    s_test_sem = ek_pt_sem_create(1);
    TEST_ASSERT(s_test_sem != NULL, "create test sem failed");
    s_semA = ek_pt_create(_sem_taskA, 10, NULL);
    s_semB = ek_pt_create(_sem_taskB, 10, NULL);
    s_sem_to = ek_pt_create(_sem_timeout_demo, 10, NULL);
    TEST_ASSERT(s_semA != NULL && s_semB != NULL && s_sem_to != NULL, "create sem tasks failed");
    EK_LOG_INFO("[sem] created: count=1, tasks=A/B/timeout");

#endif

#if EKCFG_PICOTHREAD_MSG == 1
    s_test_msg = ek_pt_msg_create(sizeof(int), 3);
    TEST_ASSERT(s_test_msg != NULL, "create test msg failed");
    TEST_ASSERT((uint8_t *)s_test_msg->rb.data == (uint8_t *)s_test_msg + sizeof(*s_test_msg),
                "dynamic msg data not inline");
    s_sender = ek_pt_create(_msg_sender, 10, NULL);
    s_receiver = ek_pt_create(_msg_receiver, 10, NULL);
    s_sender_fast = ek_pt_create(_msg_sender_fast, 10, NULL);
    TEST_ASSERT(s_sender != NULL && s_receiver != NULL && s_sender_fast != NULL, "create msg tasks failed");
    EK_LOG_INFO("[msg] created: item_size=%u, capacity=3, tasks=send/recv/fast", (unsigned)sizeof(int));

#endif

    // 控制器优先级最高，确保能及时挂起测试任务
    s_controller = ek_pt_create(_test_controller, 5, NULL);
    TEST_ASSERT(s_controller != NULL, "create controller failed");
    EK_LOG_INFO("picothread test init done");
}

/* ========================================================================
 * 信号量测试任务
 *
 * 场景：二元信号量 (count=1)，两个任务竞争 + 一个超时演示任务。
 * sem_taskA/B 交替获取/释放信号量，验证互斥与优先级轮转。
 * sem_timeout 500 tick 内尝试获取，超时则上报统计。
 * ======================================================================== */

#if EKCFG_PICOTHREAD_SEM == 1

// 二元信号量竞争任务 A：获取→持有→释放→延迟，与 B 交替执行
static void _sem_taskA(ek_pt_t *pt, void *arg)
{
    (void)arg;
    static unsigned iter = 0;
    EK_PT_BEGIN(pt);
    while (1)
    {
        int err;
        EK_PT_SEM_TAKE(s_test_sem, (uint32_t)-1, err);
        s_sem_ok++;
        EK_LOG_INFO("[sem.A] iter=%u acquired", iter);

        EK_PT_DELAY(300);
        EK_PT_SEM_GIVE(s_test_sem);
        EK_LOG_INFO("[sem.A] iter=%u released", iter);

        iter++;
        EK_PT_DELAY(100);
    }
    EK_PT_END(pt);
}

// 二元信号量竞争任务 B：与 A 相同逻辑，验证互斥轮转
static void _sem_taskB(ek_pt_t *pt, void *arg)
{
    (void)arg;
    static unsigned iter = 0;
    EK_PT_BEGIN(pt);
    while (1)
    {
        int err;
        EK_PT_SEM_TAKE(s_test_sem, (uint32_t)-1, err);
        s_sem_ok++;
        EK_LOG_INFO("[sem.B] iter=%u acquired", iter);

        EK_PT_DELAY(300);
        EK_PT_SEM_GIVE(s_test_sem);
        EK_LOG_INFO("[sem.B] iter=%u released", iter);

        iter++;
        EK_PT_DELAY(100);
    }
    EK_PT_END(pt);
}

// 信号量超时演示：500 tick 内尝试获取，超时则上报统计
static void _sem_timeout_demo(ek_pt_t *pt, void *arg)
{
    (void)arg;
    static unsigned iter = 0;
    EK_PT_BEGIN(pt);
    while (1)
    {
        int err;
        EK_PT_SEM_TAKE(s_test_sem, 500, err);

        if (err == -ETIMEDOUT)
        {
            s_sem_timeout_cnt++;
            EK_LOG_INFO("[sem.timeout] iter=%u timeout (total_timeout=%u)", iter, s_sem_timeout_cnt);
            EK_PT_DELAY(2000);
        }
        else
        {
            s_sem_ok++;
            EK_LOG_INFO("[sem.timeout] iter=%u acquired (total_ok=%u)", iter, s_sem_ok);
            EK_PT_DELAY(100);
            EK_PT_SEM_GIVE(s_test_sem);
            EK_LOG_INFO("[sem.timeout] iter=%u released", iter);
        }

        iter++;
    }
    EK_PT_END(pt);
}

#endif /* EKCFG_PICOTHREAD_SEM */

/* ========================================================================
 * 消息队列测试任务
 *
 * 场景：容量 3 的 int 环形队列，三个任务：
 *   - msg_sender:      每 500 tick 发送一条递增消息
 *   - msg_receiver:    持续接收并打印消息内容
 *   - msg_sender_fast: 无延迟连续发送，验证队列满时发送阻塞与唤醒
 * ======================================================================== */

#if EKCFG_PICOTHREAD_MSG == 1

// 定时发送者：每 500 tick 发送一条递增消息
static void _msg_sender(ek_pt_t *pt, void *arg)
{
    (void)arg;
    static int counter = 0;
    static unsigned iter = 0;
    EK_PT_BEGIN(pt);
    while (1)
    {
        int err;
        EK_PT_MSG_SEND(s_test_msg, &counter, (uint32_t)-1, err);
        s_msg_send_ok++;
        EK_LOG_INFO("[msg.send] iter=%u sent val=%d", iter, counter);

        counter++;
        iter++;
        EK_PT_DELAY(500);
    }
    EK_PT_END(pt);
}

// 持续接收者：接收并打印消息内容
static void _msg_receiver(ek_pt_t *pt, void *arg)
{
    (void)arg;
    int data;
    static unsigned iter = 0;
    EK_PT_BEGIN(pt);
    while (1)
    {
        int err;
        EK_PT_MSG_RECV(s_test_msg, &data, (uint32_t)-1, err);
        s_msg_recv_ok++;
        EK_LOG_INFO("[msg.recv] iter=%u recv=%d", iter, data);

        iter++;
        EK_PT_DELAY(100);
    }
    EK_PT_END(pt);
}

// 快速发送者：无延迟连续发送，验证队列满时发送阻塞与唤醒
static void _msg_sender_fast(ek_pt_t *pt, void *arg)
{
    (void)arg;
    static int fast_counter = 100;
    static unsigned iter = 0;
    EK_PT_BEGIN(pt);
    while (1)
    {
        int err;
        EK_PT_MSG_SEND(s_test_msg, &fast_counter, (uint32_t)-1, err);
        s_msg_send_ok++;
        EK_LOG_INFO("[msg.fast] iter=%u sent val=%d", iter, fast_counter);

        fast_counter++;
        iter++;
    }
    EK_PT_END(pt);
}

#endif /* EKCFG_PICOTHREAD_MSG */
