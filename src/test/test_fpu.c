#include "test.h"

#if EKCFG_RTOS == 1 && defined(__ARM_FP) && (__ARM_FP != 0)

#    include "ek/core/arch.h"
#    include "ek/os/kernel.h"
#    include "ek/os/thread.h"

EK_LOG_MODULE(__FILE_NAME__, EK_LOG_LEVEL_INFO);

#    define FPU_TEST_ROUNDS     (6U)
#    define FPU_RM_TEST_ROUNDS  (40U)
#    define FPU_TEST_STACK_SIZE (1024U)
#    define FPU_PRIO_PREEMPT    (4U)
#    define FPU_PRIO_INT_ONLY   (5U)
#    define FPU_PRIO_VICTIM     (6U)
#    define FPU_PRIO_COLLAB     (5U)

static volatile uint32_t s_preempt_cnt;
static volatile bool s_victim_armed;

static uint32_t s_victim_fail_cnt;
static int s_victim_bad_idx;
static uint32_t s_victim_expect;
static uint32_t s_victim_actual;

static uint32_t s_preempt_fail_cnt;
static uint32_t s_int_accum;
static uint32_t s_int_fpca_dirty;

static uint32_t s_rm_up_fpscr_err;
static uint32_t s_rm_dn_fpscr_err;
static uint32_t s_rm_up_bits;
static uint32_t s_rm_dn_bits;
static volatile uint32_t s_rm_up_done;
static volatile uint32_t s_rm_dn_done;

#    if EKCFG_OS_KERNEL_STACKOVERFLOW_CHECK == 2
static size_t s_fpu_hwm_used;
static size_t s_fpu_hwm_total;
static size_t s_int_hwm_used;
static size_t s_int_hwm_total;
#    endif

static inline uint32_t _get_fpscr(void)
{
    uint32_t fpscr;
    __EK_ASM("vmrs %0, fpscr" : "=r"(fpscr));
    return fpscr;
}

static inline void _set_fpscr(uint32_t fpscr)
{
    __EK_ASM("vmsr fpscr, %0" : : "r"(fpscr) : "cc");
}

// 装载全部 32 个单精度浮点寄存器后自旋等待外部抢占计数达标，随后一次性读回校验
__attribute__((noinline)) static void _fpu_hold_regs_until(const uint32_t in_regs[32],
                                                           uint32_t out_regs[32],
                                                           volatile uint32_t *counter,
                                                           uint32_t target,
                                                           volatile bool *armed)
{
    uint32_t tmp;
    uint32_t one = 1U;

    __EK_ASM("vldmia %[in], {s0-s31}\n\t"
             "dmb sy\n\t"
             "strb %[one], [%[arm]]\n\t"
             "1:\n\t"
             "ldr %[tmp], [%[cnt]]\n\t"
             "cmp %[tmp], %[tgt]\n\t"
             "bcc 1b\n\t"
             "vstmia %[out], {s0-s31}\n\t" : [tmp] "=&r"(tmp) : [in] "r"(in_regs),
             [out] "r"(out_regs),
             [cnt] "r"(counter),
             [tgt] "r"(target),
             [arm] "r"(armed),
             [one] "r"(one) : "s0",
             "s1",
             "s2",
             "s3",
             "s4",
             "s5",
             "s6",
             "s7",
             "s8",
             "s9",
             "s10",
             "s11",
             "s12",
             "s13",
             "s14",
             "s15",
             "s16",
             "s17",
             "s18",
             "s19",
             "s20",
             "s21",
             "s22",
             "s23",
             "s24",
             "s25",
             "s26",
             "s27",
             "s28",
             "s29",
             "s30",
             "s31",
             "cc",
             "memory");
}

// 用另一套模式彻底覆写全部 s0-s31 浮点寄存器并执行一次向量写回自检
__attribute__((noinline)) static void _fpu_clobber_regs(const uint32_t pattern[32], uint32_t verify_out[32])
{
    __EK_ASM("vldmia %[pat], {s0-s31}\n\t"
             "vstmia %[out], {s0-s31}\n\t" : : [pat] "r"(pattern),
             [out] "r"(verify_out) : "s0",
             "s1",
             "s2",
             "s3",
             "s4",
             "s5",
             "s6",
             "s7",
             "s8",
             "s9",
             "s10",
             "s11",
             "s12",
             "s13",
             "s14",
             "s15",
             "s16",
             "s17",
             "s18",
             "s19",
             "s20",
             "s21",
             "s22",
             "s23",
             "s24",
             "s25",
             "s26",
             "s27",
             "s28",
             "s29",
             "s30",
             "s31",
             "memory");
}

static void *_fpu_victim_thread(void *arg)
{
    __EK_UNUSED(arg);

    uint32_t in_regs[32];
    uint32_t out_regs[32];

    for (uint32_t i = 0U; i < 32U; i++)
    {
        in_regs[i] = 0x3F800000U | (0x00110000U + i);
        out_regs[i] = 0U;
    }

    _fpu_hold_regs_until(in_regs, out_regs, &s_preempt_cnt, FPU_TEST_ROUNDS, &s_victim_armed);

    for (uint32_t i = 0U; i < 32U; i++)
    {
        if (out_regs[i] != in_regs[i])
        {
            if (s_victim_bad_idx < 0)
            {
                s_victim_bad_idx = (int)i;
                s_victim_expect = in_regs[i];
                s_victim_actual = out_regs[i];
            }
            s_victim_fail_cnt++;
        }
    }

#    if EKCFG_OS_KERNEL_STACKOVERFLOW_CHECK == 2
    (void)ek_os_thread_stack_high_water_mark(ek_os_kernel_current_tid(), &s_fpu_hwm_used, &s_fpu_hwm_total);
#    endif

    return NULL;
}

static void *_fpu_preempt_thread(void *arg)
{
    __EK_UNUSED(arg);

    uint32_t pat[32];
    uint32_t out[32];

    // 等待低优先级 victim 完成 s0-s31 装载后再发起抢占扰动
    while (!s_victim_armed)
    {
        ek_os_thread_sleep(EK_OS_MSEC(1));
    }

    for (uint32_t r = 0U; r < FPU_TEST_ROUNDS; r++)
    {
        ek_os_thread_sleep(EK_OS_MSEC(3));

        for (uint32_t i = 0U; i < 32U; i++)
        {
            pat[i] = 0x40000000U | (r << 8) | i;
            out[i] = 0U;
        }
        _fpu_clobber_regs(pat, out);

        // 在本地再额外触发一次浮点计算确保硬件和扩展上下文状态均被改动
        volatile float f0 = (float)(r + 1U) * 1.25f;
        volatile float f1 = f0 + 0.75f;
        __EK_UNUSED(f1);

        for (uint32_t i = 0U; i < 32U; i++)
        {
            if (out[i] != pat[i])
            {
                s_preempt_fail_cnt++;
            }
        }

        s_preempt_cnt = r + 1U;
    }

    return NULL;
}

// 纯整数中优先级线程，在 FPU 线程之间制造扩展帧与基础帧交替切换
static void *_int_only_thread(void *arg)
{
    __EK_UNUSED(arg);

    while (!s_victim_armed)
    {
        ek_os_thread_sleep(EK_OS_MSEC(1));
    }

    uint32_t acc = 0U;
    for (uint32_t r = 1U; r <= FPU_TEST_ROUNDS; r++)
    {
        ek_os_thread_sleep(EK_OS_MSEC(2));
        if ((__get_CONTROL() & CONTROL_FPCA_Msk) != 0U)
        {
            s_int_fpca_dirty++;
        }
        acc += r * 17U;
    }
    s_int_accum = acc;

#    if EKCFG_OS_KERNEL_STACKOVERFLOW_CHECK == 2
    (void)ek_os_thread_stack_high_water_mark(ek_os_kernel_current_tid(), &s_int_hwm_used, &s_int_hwm_total);
#    endif

    return NULL;
}

// 协作验证各自线程 FPSCR 舍入模式与计算结果在频繁切换下保持隔离
static void *_fpu_rmode_worker(void *arg)
{
    uint32_t mode = (uint32_t)(uintptr_t)arg;
    uint32_t saved_fpscr = _get_fpscr();
    uint32_t target_rmode = (mode & 0x3U) << 22U;
    uint32_t fpscr_err = 0U;
    uint32_t last_bits = 0U;

    _set_fpscr((saved_fpscr & ~(0x3UL << 22U)) | target_rmode);

    for (uint32_t i = 0U; i < FPU_RM_TEST_ROUNDS; i++)
    {
        volatile float a = 1.0f;
        volatile float b = 3.0f;
        float res = a / b;
        uint32_t bits = 0U;
        memcpy(&bits, &res, sizeof(bits));
        last_bits = bits;

        uint32_t cur = _get_fpscr();
        if ((cur & (0x3UL << 22U)) != target_rmode)
        {
            fpscr_err++;
        }

        ek_os_thread_yield();
    }

    _set_fpscr(saved_fpscr);

    if (mode == 1U)
    {
        s_rm_up_fpscr_err = fpscr_err;
        s_rm_up_bits = last_bits;
        s_rm_up_done = 1U;
    }
    else
    {
        s_rm_dn_fpscr_err = fpscr_err;
        s_rm_dn_bits = last_bits;
        s_rm_dn_done = 1U;
    }

    return NULL;
}

int fpu_test(void)
{
    uint32_t total_fail = 0U;
    const size_t unused_before = ek_heap_unused();

    s_preempt_cnt = 0U;
    s_victim_armed = false;
    s_victim_fail_cnt = 0U;
    s_victim_bad_idx = -1;
    s_victim_expect = 0U;
    s_victim_actual = 0U;
    s_preempt_fail_cnt = 0U;
    s_int_accum = 0U;
    s_int_fpca_dirty = 0U;

    EK_LOG_INFO("==== fpu_test started ====");

    // 先创建高/中优先级扰动线程并让其睡眠等待，再由 victim 进入纯汇编等待状态
    int tid_preempt = ek_os_thread_create("fpu_pre", _fpu_preempt_thread, NULL, FPU_PRIO_PREEMPT, FPU_TEST_STACK_SIZE);
    int tid_int = ek_os_thread_create("int_only", _int_only_thread, NULL, FPU_PRIO_INT_ONLY, FPU_TEST_STACK_SIZE);
    int tid_victim = ek_os_thread_create("fpu_vic", _fpu_victim_thread, NULL, FPU_PRIO_VICTIM, FPU_TEST_STACK_SIZE);

    if (tid_preempt < 0 || tid_int < 0 || tid_victim < 0)
    {
        EK_LOG_ERROR("FAIL: create preempt/int/victim threads (%d, %d, %d)", tid_preempt, tid_int, tid_victim);
        return -ENOMEM;
    }

    ek_os_thread_join(tid_victim, EK_OS_FOREVER, NULL);
    ek_os_thread_join(tid_int, EK_OS_FOREVER, NULL);
    ek_os_thread_join(tid_preempt, EK_OS_FOREVER, NULL);

    if (s_victim_fail_cnt == 0U && s_preempt_cnt == FPU_TEST_ROUNDS)
    {
        EK_LOG_INFO("PASS: async preempt s0-s31 intact across %lu rounds", (unsigned long)s_preempt_cnt);
    }
    else
    {
        total_fail++;
        EK_LOG_ERROR("FAIL: s0-s31 mismatch cnt=%lu bad=s%d expect=0x%08lx actual=0x%08lx",
                     (unsigned long)s_victim_fail_cnt,
                     s_victim_bad_idx,
                     (unsigned long)s_victim_expect,
                     (unsigned long)s_victim_actual);
    }

    if (s_preempt_fail_cnt == 0U)
    {
        EK_LOG_INFO("PASS: preempt thread s0-s31 self-check intact");
    }
    else
    {
        total_fail++;
        EK_LOG_ERROR("FAIL: preempt thread s0-s31 mismatch=%lu", (unsigned long)s_preempt_fail_cnt);
    }

    uint32_t expected_int = ((FPU_TEST_ROUNDS * (FPU_TEST_ROUNDS + 1U)) / 2U) * 17U;
    if (s_int_accum == expected_int && s_int_fpca_dirty == 0U)
    {
        EK_LOG_INFO("PASS: int-only mixed switch accum=%lu fpca_dirty=0", (unsigned long)s_int_accum);
    }
    else
    {
        total_fail++;
        EK_LOG_ERROR("FAIL: int-only accum=%lu (expect %lu) fpca_dirty=%lu",
                     (unsigned long)s_int_accum,
                     (unsigned long)expected_int,
                     (unsigned long)s_int_fpca_dirty);
    }

    s_rm_up_fpscr_err = 0U;
    s_rm_dn_fpscr_err = 0U;
    s_rm_up_bits = 0U;
    s_rm_dn_bits = 0U;
    s_rm_up_done = 0U;
    s_rm_dn_done = 0U;

    int tid_up =
        ek_os_thread_create("fpu_rp", _fpu_rmode_worker, (void *)(uintptr_t)1U, FPU_PRIO_COLLAB, FPU_TEST_STACK_SIZE);
    int tid_dn =
        ek_os_thread_create("fpu_rm", _fpu_rmode_worker, (void *)(uintptr_t)2U, FPU_PRIO_COLLAB, FPU_TEST_STACK_SIZE);
    if (tid_up < 0 || tid_dn < 0)
    {
        EK_LOG_ERROR("FAIL: create rmode worker threads (%d, %d)", tid_up, tid_dn);
        return -ENOMEM;
    }

    ek_os_thread_join(tid_up, EK_OS_FOREVER, NULL);
    ek_os_thread_join(tid_dn, EK_OS_FOREVER, NULL);

    if (s_rm_up_done == 1U && s_rm_dn_done == 1U && s_rm_up_fpscr_err == 0U && s_rm_dn_fpscr_err == 0U)
    {
        EK_LOG_INFO("PASS: FPSCR RMode isolated (RP=0x%08lx RM=0x%08lx)",
                    (unsigned long)s_rm_up_bits,
                    (unsigned long)s_rm_dn_bits);
    }
    else
    {
        total_fail++;
        EK_LOG_ERROR("FAIL: FPSCR RMode corrupted up_err=%lu dn_err=%lu",
                     (unsigned long)s_rm_up_fpscr_err,
                     (unsigned long)s_rm_dn_fpscr_err);
    }

    if (s_rm_up_bits > s_rm_dn_bits)
    {
        EK_LOG_INFO("PASS: FPU rounding isolation 1/3 RP(0x%08lx) > RM(0x%08lx)",
                    (unsigned long)s_rm_up_bits,
                    (unsigned long)s_rm_dn_bits);
    }
    else
    {
        total_fail++;
        EK_LOG_ERROR("FAIL: FPU rounding mismatch RP=0x%08lx RM=0x%08lx",
                     (unsigned long)s_rm_up_bits,
                     (unsigned long)s_rm_dn_bits);
    }

#    if EKCFG_OS_KERNEL_STACKOVERFLOW_CHECK == 2
    EK_LOG_INFO("HWM: fpu_victim=%u/%u B, int_only=%u/%u B (delta=%d B)",
                (unsigned)s_fpu_hwm_used,
                (unsigned)s_fpu_hwm_total,
                (unsigned)s_int_hwm_used,
                (unsigned)s_int_hwm_total,
                (int)s_fpu_hwm_used - (int)s_int_hwm_used);
#    endif

    const size_t unused_after = ek_heap_unused();
    if (unused_after == unused_before)
    {
        EK_LOG_INFO("PASS: heap unused unchanged (%u B)", (unsigned)unused_after);
    }
    else
    {
        total_fail++;
        EK_LOG_ERROR("FAIL: heap leaked (%u -> %u B)", (unsigned)unused_before, (unsigned)unused_after);
    }

    EK_LOG_INFO("fpu_test finished: %s (%lu fail)", total_fail ? "FAILED" : "ALL PASS", (unsigned long)total_fail);
    return (int)total_fail;
}

#else

int fpu_test(void)
{
    return 0;
}

#endif /* EKCFG_RTOS == 1 && defined(__ARM_FP) && (__ARM_FP != 0) */
