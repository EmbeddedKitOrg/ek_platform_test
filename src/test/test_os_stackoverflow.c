/**
 * @file test_os_stackoverflow.c
 * @brief RTOS 栈溢出检测（级别 1/2）、高水位线查询及空闲钩子自检（适用于 Renode 与实机）
 */

#include "test.h"

#if EKCFG_RTOS == 1 && defined(EK_RUN_STACKOVERFLOW_TEST)

#    include "ek/os/kernel.h"
#    include "ek/os/sem.h"
#    include "ek/os/thread.h"

EK_LOG_MODULE(__FILE_NAME__, EK_LOG_LEVEL_DEBUG);

#    define SOF_PRIO_CTRL (4U)
#    define SOF_PRIO_WORK (6U)
#    define SOF_CANARY    (0xDEADBEEFUL)

static uint32_t s_total;
static uint32_t s_fail;

#    define SOF_CHECK(cond, msg)                           \
        do                                                 \
        {                                                  \
            s_total++;                                     \
            if (cond)                                      \
            {                                              \
                EK_LOG_INFO("PASS: %s", (msg));            \
            }                                              \
            else                                           \
            {                                              \
                s_fail++;                                  \
                EK_LOG_ERROR("FAIL: %s", (msg));           \
            }                                              \
        } while (0)

static volatile uint32_t s_idle_cnt;
static volatile uint32_t s_ovf_cnt;
static volatile int s_ovf_tid;
static char s_ovf_name[EKCFG_OS_THREAD_NAME_LEN];

void ek_os_kernel_idle_hook(void)
{
    s_idle_cnt++;
}

#    if EKCFG_OS_KERNEL_STACKOVERFLOW_CHECK > 0
void ek_os_kernel_stackoverflow_hook(int tid, const char *name)
{
    s_ovf_cnt++;
    s_ovf_tid = tid;
    if (name)
    {
        size_t i = 0U;
        for (; i < (sizeof(s_ovf_name) - 1U) && name[i] != '\0'; i++)
        {
            s_ovf_name[i] = name[i];
        }
        s_ovf_name[i] = '\0';
    }
    else
    {
        s_ovf_name[0] = '\0';
    }
}
#    endif

/* 故意溢栈专用隔离缓冲：低地址布置 512 B 安全溢栈垫与金丝雀，保护其余静态数据不被踩坏 */
static struct
{
    uint32_t canary[4];
    uint8_t underflow_pad[512];
    uint8_t stack[512 + EK_OS_THREAD_TCB_SIZE];
} s_ovf_buf __EK_ALIGNED(8);

static void _reset_ovf_capture(void)
{
    s_ovf_cnt = 0U;
    s_ovf_tid = -1;
    memset(s_ovf_name, 0, sizeof(s_ovf_name));
    for (size_t i = 0U; i < 4U; i++)
    {
        s_ovf_buf.canary[i] = SOF_CANARY;
    }
}

static bool _canary_intact(void)
{
    for (size_t i = 0U; i < 4U; i++)
    {
        if (s_ovf_buf.canary[i] != SOF_CANARY) return false;
    }
    return true;
}

__attribute__((noinline)) static void _recurse_until_underflow(int depth)
{
    volatile uint8_t frame[64];
    frame[0] = (uint8_t)depth;
    frame[sizeof(frame) - 1U] = (uint8_t)(depth ^ 0x5AU);

    if ((uintptr_t)&frame[0] < (uintptr_t)s_ovf_buf.stack || depth >= 24)
    {
        ek_os_thread_sleep(EK_OS_MSEC(5));
        return;
    }

    _recurse_until_underflow(depth + 1);
    (void)frame[0];
}

static void *_lvl1_worker(void *arg)
{
    (void)arg;
    _recurse_until_underflow(0);
    return NULL;
}

#    if EKCFG_OS_KERNEL_STACKOVERFLOW_CHECK == 2

static ek_os_sem_t *s_hwm_sem1;
static ek_os_sem_t *s_hwm_sem2;
static volatile bool s_hwm_ready;

__attribute__((noinline)) static void _deep_stack_call(void)
{
    volatile uint8_t buf[320];
    for (size_t i = 0U; i < sizeof(buf); i++)
    {
        buf[i] = (uint8_t)0x3CU;
    }
    (void)buf[sizeof(buf) - 1U];
}

static void *_hwm_worker(void *arg)
{
    (void)arg;
    s_hwm_ready = true;
    ek_os_sem_take(s_hwm_sem1, EK_OS_FOREVER);
    _deep_stack_call();
    ek_os_sem_take(s_hwm_sem2, EK_OS_FOREVER);
    return NULL;
}

__attribute__((noinline)) static void _touch_stack_bottom_guard(void)
{
    volatile uint8_t *guard = (volatile uint8_t *)s_ovf_buf.stack;
    for (size_t i = 0U; i < 16U; i++)
    {
        guard[i] = (uint8_t)(0x50U + i);
    }
}

static void *_lvl2_guard_worker(void *arg)
{
    (void)arg;
    _touch_stack_bottom_guard();
    /* 返回后 SP 仍在合法区间，由级别 2 栈底魔数哨兵在切出时捕获 */
    ek_os_thread_sleep(EK_OS_MSEC(5));
    return NULL;
}

#    endif /* EKCFG_OS_KERNEL_STACKOVERFLOW_CHECK == 2 */

static void *_sof_test_main(void *arg)
{
    (void)arg;
    s_total = 0U;
    s_fail = 0U;

    EK_LOG_INFO("==== os_stackoverflow_test started (CHECK=%u) ====",
                (unsigned int)EKCFG_OS_KERNEL_STACKOVERFLOW_CHECK);

    /* 验证空闲钩子正常触发，且正常运行不会误触发溢出钩子 */
    _reset_ovf_capture();
    s_idle_cnt = 0U;
    ek_os_thread_sleep(EK_OS_MSEC(10));
    EK_LOG_INFO("idle_hook count=%lu", (unsigned long)s_idle_cnt);
    SOF_CHECK(s_idle_cnt > 0U, "idle hook invoked while sleeping");
    SOF_CHECK(s_ovf_cnt == 0U, "no false stack overflow during normal execution");

#    if EKCFG_OS_KERNEL_STACKOVERFLOW_CHECK > 0
    /* 级别 1/2 通用：真实递归使 SP 越过 stack.base 后切出，触发溢出钩子 */
    _reset_ovf_capture();
    int ovf1_tid = ek_os_thread_create_static(
        "ovf_lvl1", _lvl1_worker, NULL, SOF_PRIO_WORK, s_ovf_buf.stack, sizeof(s_ovf_buf.stack));
    SOF_CHECK(ovf1_tid > 0, "create static lvl1 overflow worker");
    if (ovf1_tid > 0)
    {
        SOF_CHECK(ek_os_thread_join(ovf1_tid, EK_OS_MSEC(500), NULL) == 0, "join lvl1 overflow worker");
        EK_LOG_INFO("lvl1 hook: cnt=%lu tid=%d (expect %d) name=%s",
                    (unsigned long)s_ovf_cnt,
                    s_ovf_tid,
                    ovf1_tid,
                    s_ovf_name);
        SOF_CHECK(s_ovf_cnt >= 1U && s_ovf_tid == ovf1_tid && strcmp(s_ovf_name, "ovf_lvl1") == 0,
                  "lvl1 captured sp < base overflow with exact tid and name");
        SOF_CHECK(_canary_intact(), "lvl1 overflow remained safely inside underflow_pad");
    }
#    endif

#    if EKCFG_OS_KERNEL_STACKOVERFLOW_CHECK == 2
    /* 级别 2：高水位线查询，深栈函数返回后峰值仍应被保留 */
    s_hwm_sem1 = ek_os_sem_create(0);
    s_hwm_sem2 = ek_os_sem_create(0);
    s_hwm_ready = false;

    int hwm_tid = ek_os_thread_create("hwm_thrd", _hwm_worker, NULL, SOF_PRIO_WORK, 1024U);
    ek_os_thread_sleep(EK_OS_MSEC(10));
    SOF_CHECK(hwm_tid > 0 && s_hwm_ready, "hwm worker blocked on sem1");

    size_t used_base = 0U;
    size_t total_sz = 0U;
    SOF_CHECK(ek_os_thread_stack_high_water_mark(hwm_tid, &used_base, &total_sz) == 0 && total_sz >= 1024U &&
                  used_base > 0U && used_base < total_sz,
              "hwm baseline query ok");

    ek_os_sem_give(s_hwm_sem1);
    ek_os_thread_sleep(EK_OS_MSEC(10));

    size_t used_peak = 0U;
    SOF_CHECK(ek_os_thread_stack_high_water_mark(hwm_tid, &used_peak, NULL) == 0 &&
                  used_peak > used_base && used_peak >= 320U && used_peak < total_sz,
              "hwm peak captures returned deep frame");
    EK_LOG_INFO("hwm stats: base=%zu peak=%zu total=%zu", used_base, used_peak, total_sz);

    SOF_CHECK(ek_os_thread_stack_high_water_mark(hwm_tid, NULL, NULL) == -EINVAL, "hwm(NULL, NULL) returns -EINVAL");
    SOF_CHECK(ek_os_thread_stack_high_water_mark(9999, &used_peak, &total_sz) == -ESRCH,
              "hwm(bad tid) returns -ESRCH");

    ek_os_sem_give(s_hwm_sem2);
    SOF_CHECK(ek_os_thread_join(hwm_tid, EK_OS_MSEC(500), NULL) == 0, "join hwm worker");
    ek_os_sem_destroy(s_hwm_sem1);
    ek_os_sem_destroy(s_hwm_sem2);

    /* 级别 2 独有：栈底 16 字节哨兵捕获“切出前已返回”的瞬时溢栈 */
    _reset_ovf_capture();
    int ovf2_tid = ek_os_thread_create_static(
        "ovf_lvl2", _lvl2_guard_worker, NULL, SOF_PRIO_WORK, s_ovf_buf.stack, sizeof(s_ovf_buf.stack));
    SOF_CHECK(ovf2_tid > 0, "create static lvl2 guard worker");
    if (ovf2_tid > 0)
    {
        SOF_CHECK(ek_os_thread_join(ovf2_tid, EK_OS_MSEC(500), NULL) == 0, "join lvl2 guard worker");
        EK_LOG_INFO("lvl2 hook: cnt=%lu tid=%d (expect %d) name=%s",
                    (unsigned long)s_ovf_cnt,
                    s_ovf_tid,
                    ovf2_tid,
                    s_ovf_name);
        SOF_CHECK(s_ovf_cnt >= 1U && s_ovf_tid == ovf2_tid && strcmp(s_ovf_name, "ovf_lvl2") == 0,
                  "lvl2 magic sentinel captured transient bottom-guard overwrite");
    }
#    endif

    uint32_t passed = s_total - s_fail;
    EK_LOG_INFO("stackoverflow_test finished: %s (%lu/%lu passed)",
                s_fail ? "FAILED" : "ALL PASS",
                (unsigned long)passed,
                (unsigned long)s_total);
    return NULL;
}

void os_stackoverflow_test(void)
{
    (void)ek_os_thread_create("sof_main", _sof_test_main, NULL, SOF_PRIO_CTRL, 1536U);
}

#else

void os_stackoverflow_test(void)
{
}

#endif /* EKCFG_RTOS == 1 && EK_RUN_STACKOVERFLOW_TEST */
