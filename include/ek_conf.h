/**
 * @file ek_conf.h 模板
 *
 * 使用方法:
 *   1. 将此文件复制到你的项目 include 路径中，命名为 ek_conf.h
 *   2. 确保你的 include 路径优先级高于 ek_utils/inc/
 *   3. 只保留你需要覆盖的宏，删除其余行
 *      （未定义的宏会从 ek_conf_default.h 取默认值）
 */

#ifndef EK_CONF_H
#define EK_CONF_H

/* ========================================================================
 * 平台/运行环境
 * ======================================================================== */

#define EKCFG_RTOS         (0)
#define EKCFG_PICOLIBC     (1)
#define EKCFG_IO_LWPRTF    (0)
#define EKCFG_STATIC_ALLOC (1)
/* ========================================================================
 * 核心服务
 * ======================================================================== */

#define EKCFG_EXPORT (1)
#define EKCFG_ASSERT (1)
#define EKCFG_LOG    (1)

/* ========================================================================
 * 数据结构
 * ======================================================================== */

#define EKCFG_STR            (1)
#define EKCFG_STR_VIEW       (1)
#define EKCFG_LIST           (1)
#define EKCFG_VEC            (1)
#define EKCFG_RINGBUF        (1)
#define EKCFG_RINGBUF_SPSC   (1)
#define EKCFG_STACK          (1)
#define EKCFG_SNAPSHOT       (1)
#define EKCFG_EVOKE          (1)
#define EKCFG_PICOTHREAD     (1)
#define EKCFG_PICOTHREAD_SEM (1)
#define EKCFG_PICOTHREAD_MSG (1)
#define EKCFG_VFS            (1)

/* ========================================================================
 * 模块子配置 — 根据实际硬件调整
 * ======================================================================== */

#define EKCFG_ARCH_IRQ_COUNT     (91)
#define EKCFG_HEAP_TLSF          (1)
#define EKCFG_HEAP_SIZE          (64 * 1024)
#define EKCFG_HEAP_SECTION       ".tcmram"
#define EKCFG_LOG_DEBUG          (1)
#define EKCFG_LOG_COLOR          (1)
#define EKCFG_LOG_BUF_SIZE       (128)
#define EKCFG_ASSERT_TINY        (1)
#define EKCFG_ASSERT_LOG         (1)
#define EKCFG_VFS_TREE_DUMP_UTF8 (1)

#endif /* EK_CONF_H */
