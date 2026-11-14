/**
 * @file ek_board.h
 * @brief 板级内存布局（GD32F470ZG）
 *
 * 由 ek_platform/cmake/linker.cmake 读取，用于生成链接脚本。
 * 功能开关在 ek_conf.h，本文件不参与组件裁剪。
 */

#ifndef EK_BOARD_H
#define EK_BOARD_H

/* FLASH */
#define EKLD_FLASH_ORIGIN (0x08000000)
#define EKLD_FLASH_LENGTH (1024 * 1024)

/* 主 RAM */
#define EKLD_RAM_ORIGIN (0x20000000)
#define EKLD_RAM_LENGTH (448 * 1024)

/* TCMRAM */
#define EKLD_TCMRAM_ORIGIN (0x10000000)
#define EKLD_TCMRAM_LENGTH (64 * 1024)

/* SDRAM0 / SDRAM1（EXMC device0 / device1，LENGTH 为 0 表示未使用） */
#define EKLD_SDRAM0_ORIGIN (0xC0000000)
#define EKLD_SDRAM0_LENGTH (32 * 1024 * 1024)

#define EKLD_SDRAM1_ORIGIN (0xD0000000)
#define EKLD_SDRAM1_LENGTH (0)

/* 栈与最小堆 */
#define EKLD_STACK_SIZE    (0x400)
#define EKLD_MIN_HEAP_SIZE (0x200)

#endif /* EK_BOARD_H */
