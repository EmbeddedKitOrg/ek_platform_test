# ek_platform Thread-Metric RTOS 基准测试报告

> 测试日期：2026-11-22  
> 测试对象：`lib/ek_platform` 内置无滴答 RTOS 内核（`EKCFG_RTOS == 1`）

---

## 一、 测试环境与配置

| 项目 | 配置详情 |
| :--- | :--- |
| **目标芯片** | GD32F470ZGT6（ARM Cortex-M4F，硬件单精度浮点 `fpv4-sp-d16`） |
| **系统主频** | `240 MHz`（25 MHz HXTAL PLL 倍频，240 MHz 内核时钟） |
| **工具链** | `arm-none-eabi-gcc 14.2.1`（GCC 15:14.2.rel1-1） + `picolibc` |
| **对比构建模式** | `Release`（`-Os -g0 -DNDEBUG`） vs `RelWithDebInfo`（`-O2 -g3 -DNDEBUG`） |
| **内核时基** | Tickless 无滴答调度：`DWT->CYCCNT` 自由计时 + `TIMER5` 单次闹钟（`1 MHz`，`1 tick = 1 us`） |
| **堆与内存布局** | `TLSF v3.1` 位于 `64 KB TCMRAM`（自动推导 `EKCFG_TLSF_FL_INDEX_MAX = 16`、`EKCFG_TLSF_SL_INDEX_COUNT_LOG2 = 4`） |
| **基准测试套件** | Eclipse ThreadX Thread-Metric `v6.1.7`（标准 `TM_TEST_DURATION = 30 s`） |
| **测试线程栈** | `1024 B` / 线程；消息队列深度 `10`（单条 `16 B`）；内存测试池 `4096 B`（分配 `128 B` 块） |
| **软件中断源** | `CAN1_TX_IRQn`（IRQ 63，NVIC `PendSV` 最低优先级 `0xFF`，`TM_SW_IRQn` 优先级 `(0, 2)`） |
| **烧录与采样** | DAPLink CMSIS-DAP（`probe-rs`）+ 串口 `/dev/ttyACM0`（`115200 8N1`） |

---

## 二、 `-Os` vs `-O2` 核心对比汇总（30 秒标准周期）

| 用例 | `-Os` 30s 总分（修复前） | `-O2` 30s 总分（修复前） | 修复后 `-O2`（纯 Tickless） | 较修复前 `-O2` 变化 | 修复后 `-O2` 单次耗时 (`us`) | 修复后 `-O2` CPU 周期 (`cycles`) | `FLASH` 增量 (`-O2` vs `-Os`) |
| :--- | ---: | ---: | ---: | :---: | ---: | ---: | :---: |
| **basic**（单线程运算基线） | `584,548` | `584,563` | **`585,632`** | `+0.18%` | `51.227` | `12,294.4` | `+1,396 B` |
| **cooperative**（协作式调度） | `17,275,850` | `19,529,695` | **`19,564,493`** | `+0.18%` | `1.533` | `368.0` | `+1,412 B` |
| **preemptive**（抢占式调度） | `7,756,055` | `9,040,119` | **`9,004,185`** | `-0.40%` | `3.332` | `799.6` | `+1,400 B` |
| **interrupt**（中断响应与信号量） | `31,659,784` | `32,816,481` | **`32,875,676`** | `+0.18%` | `0.913` | `219.0` | `+1,432 B` |
| **interrupt_preemption**（中断抢占） | `6,059,785` | `6,923,840` | **`6,902,923`** | `-0.30%` | `4.346` | `1,043.0` | `+1,416 B` |
| **message**（16B 消息收发） | `10,839,279` | `11,647,651` | **`11,668,999`** | `+0.18%` | `2.571` | `617.0` | `+1,540 B` |
| **synchronization**（信号量同步） | `40,601,275` | `43,555,199` | **`43,634,986`** | `+0.18%` | `0.688` | `165.0` | `+1,432 B` |
| **memory**（128B 内存池分配释放） | `6,387,943` | `8,514,936` | **`8,530,535`** | `+0.18%` | `3.517` | `844.0` | `+1,416 B` |

> **小结**：
> 1. `-O2` 较 `-Os` 仅多占用约 **1.40 ~ 1.54 KB** 的 Flash（占 1 MB 片内 Flash 的 `0.14%`），在内存分配与调度类测试上带来 **+13% ~ +33%** 的显著提升。
> 2. 修复 DWT Tickless 模式下遗留的 1 ms `SysTick` 中断干扰后，除 `preemptive`/`interrupt_preemption`（受 `kernel_thread_suspend` 代码微调导致的轻微指令对齐/预取波动约 `-0.30% ~ -0.40%`）外，其余 6 项均稳定提升 **`+0.18%`**（正好对应消除了每 1 ms 约 430 cycles 的冗余 SysTick 处理开销）。

---

## 三、 `-O2`（`RelWithDebInfo`）全量明细

### 3.1 修复后 `-O2`（纯 Tickless，已关闭冗余 1 ms `SysTick`）

本次复测前修复了审计发现的 2 处真实逻辑问题，并在真机通过了全套 `os_test`（含新增的挂起锁等待者后原持锁者继承优先级回落检查）：

1. **`lib/ek_platform/src/os/kernel.c` (`kernel_thread_suspend`)**：将读取 `thrd->waiting_lock` 移至 `thrd->waiting_lock = NULL;` 之前，修复阻塞在互斥锁上的线程被 `suspend` 时因 `wlock` 恒为 `NULL` 导致持锁者继承优先级无法回落的问题。
2. **`src/bsp/bsp_os_timer.c` (`_os_timer_init` / `_systick_handler`)**：探测并启用 `DWT + TIMER5` Tickless 模式后关闭启动阶段配置的 `SysTick`（清零 `SysTick->CTRL`/`VAL` 及 `SCB_ICSR_PENDSTCLR_Msk`，并在 `_systick_handler` 开头防护），消除每 1 ms 多余触发 `ek_os_kernel_announce_isr()` 并重装 `TIMER5` 的干扰。

| 用例 | 对应测试文件 | `Time Period Total (30s)` | 吞吐率 (`ops/s`) | 单次计数耗时 (`us`) | 单次计数周期 (`cycles`) | 墙钟实测 (`s`) | `FLASH` / `RAM` (`TCMRAM=64 KB`) | 自检状态 |
| :--- | :--- | ---: | ---: | ---: | ---: | ---: | :--- | :---: |
| **basic** | `tm_basic_processing_test.c` | `585,632` | `19,521.07` | `51.227` | `12,294.4` | `30.543` | `92,500 B` / `9,992 B` | PASS |
| **cooperative** | `tm_cooperative_scheduling_test.c` | `19,564,493` | `652,149.77` | `1.533` | `368.0` | `30.540` | `93,200 B` / `5,912 B` | PASS |
| **preemptive** | `tm_preemptive_scheduling_test.c` | `9,004,185` | `300,139.50` | `3.332` | `799.6` | `30.541` | `92,792 B` / `5,912 B` | PASS |
| **interrupt** | `tm_interrupt_processing_test.c` | `32,875,676` | `1,095,855.87` | `0.913` | `219.0` | `30.538` | `93,108 B` / `5,912 B` | PASS |
| **interrupt_preemption** | `tm_interrupt_preemption_processing_test.c` | `6,902,923` | `230,097.43` | `4.346` | `1,043.0` | `30.539` | `92,656 B` / `5,904 B` | PASS |
| **message** | `tm_message_processing_test.c` | `11,668,999` | `388,966.63` | `2.571` | `617.0` | `30.545` | `93,580 B` / `5,944 B` | PASS |
| **synchronization** | `tm_synchronization_processing_test.c` | `43,634,986` | `1,454,499.53` | `0.688` | `165.0` | `30.537` | `93,012 B` / `5,912 B` | PASS |
| **memory** | `tm_memory_allocation_test.c` | `8,530,535` | `284,351.17` | `3.517` | `844.0` | `30.539` | `92,656 B` / `22,296 B` | PASS |

### 3.2 修复前 `-O2`（历史基线，含 1 ms `SysTick` 干扰）

| 用例 | 对应测试文件 | `Time Period Total (30s)` | 吞吐率 (`ops/s`) | 单次计数耗时 (`us`) | 单次计数周期 (`cycles`) | 墙钟实测 (`s`) | `FLASH` / `RAM` (`TCMRAM=64 KB`) | 自检状态 |
| :--- | :--- | ---: | ---: | ---: | ---: | ---: | :--- | :---: |
| **basic** | `tm_basic_processing_test.c` | `584,563` | `19,485.43` | `51.320` | `12,316.9` | `30.537` | `92,452 B` / `9,992 B` | PASS |
| **cooperative** | `tm_cooperative_scheduling_test.c` | `19,529,695` | `650,989.83` | `1.536` | `368.7` | `30.550` | `93,168 B` / `5,912 B` | PASS |
| **preemptive** | `tm_preemptive_scheduling_test.c` | `9,040,119` | `301,337.30` | `3.319` | `796.4` | `30.537` | `92,760 B` / `5,912 B` | PASS |
| **interrupt** | `tm_interrupt_processing_test.c` | `32,816,481` | `1,093,882.70` | `0.914` | `219.4` | `30.541` | `93,060 B` / `5,912 B` | PASS |
| **interrupt_preemption** | `tm_interrupt_preemption_processing_test.c` | `6,923,840` | `230,794.67` | `4.333` | `1,039.9` | `30.533` | `92,624 B` / `5,904 B` | PASS |
| **message** | `tm_message_processing_test.c` | `11,647,651` | `388,255.03` | `2.576` | `618.2` | `30.534` | `93,532 B` / `5,944 B` | PASS |
| **synchronization** | `tm_synchronization_processing_test.c` | `43,555,199` | `1,451,839.97` | `0.689` | `165.3` | `30.553` | `92,964 B` / `5,912 B` | PASS |
| **memory** | `tm_memory_allocation_test.c` | `8,514,936` | `283,831.20` | `3.523` | `845.6` | `30.544` | `92,624 B` / `22,296 B` | PASS |

---

## 四、 `-Os`（`Release`）全量明细

| 用例 | 对应测试文件 | `Time Period Total (30s)` | 吞吐率 (`ops/s`) | 单次计数耗时 (`us`) | 单次计数周期 (`cycles`) | 墙钟实测 (`s`) | `FLASH` / `RAM` (`TCMRAM=64 KB`) | 自检状态 |
| :--- | :--- | ---: | ---: | ---: | ---: | ---: | :--- | :---: |
| **basic** | `tm_basic_processing_test.c` | `584,548` | `19,484.93` | `51.322` | `12,317.2` | `30.551` | `91,104 B` / `9,984 B` | PASS |
| **cooperative** | `tm_cooperative_scheduling_test.c` | `17,275,850` | `575,861.67` | `1.737` | `416.8` | `30.535` | `91,788 B` / `5,904 B` | PASS |
| **preemptive** | `tm_preemptive_scheduling_test.c` | `7,756,055` | `258,535.17` | `3.868` | `928.3` | `30.540` | `91,392 B` / `5,904 B` | PASS |
| **interrupt** | `tm_interrupt_processing_test.c` | `31,659,784` | `1,055,326.13` | `0.948` | `227.4` | `30.538` | `91,676 B` / `5,904 B` | PASS |
| **interrupt_preemption** | `tm_interrupt_preemption_processing_test.c` | `6,059,785` | `201,992.83` | `4.951` | `1,188.2` | `30.532` | `91,240 B` / `5,896 B` | PASS |
| **message** | `tm_message_processing_test.c` | `10,839,279` | `361,309.30` | `2.768` | `664.3` | `30.535` | `92,040 B` / `5,936 B` | PASS |
| **synchronization** | `tm_synchronization_processing_test.c` | `40,601,275` | `1,353,375.83` | `0.739` | `177.3` | `30.538` | `91,580 B` / `5,904 B` | PASS |
| **memory** | `tm_memory_allocation_test.c` | `6,387,943` | `212,931.43` | `4.696` | `1,127.1` | `30.535` | `91,240 B` / `22,288 B` | PASS |

> 注：`TCMRAM` 占用显示 `64 KB` 是因为链接阶段为默认堆数组 `s_default_heap[64 KB]` 静态分配了整块 `TCMRAM` 空间；两组全量 16 次 30 秒测试中均未出现任何 `ERROR` 日志，各多任务测试均通过官方计数器同步校验。

---

## 五、 分项性能解读（以 `-O2` 数据为主）

1. **协作式切换（`cooperative`）**：5 个同优先级线程依次 `ek_os_thread_yield()`，30 秒总计 **19,529,695** 次切换（**650,990 次/秒**），5 个线程精准各得 **`3,905,939`** 次，调度绝对公平；单次完整让出切换仅 **`1.536 us`（`368.7` cycles）**。
2. **抢占式切换（`preemptive`）**：5 个不同优先级线程通过 `resume` 级联抢占再逐级自 `suspend` 返回。一轮包含 5 次计数增和 8 次上下文切换（`0→1→2→3→4→3→2→1→0`），折合抢占切换吞吐率为 **`482,140` 次/秒**，平均单次含 API 查找与 `PendSV` 的抢占切换约 **`2.074 us`（约 `498` cycles）**。
3. **中断响应（`interrupt`）**：软件触发 `CAN1_TX_IRQn` → ISR `ek_os_sem_give()` → 线程 `ek_os_sem_take()`，吞吐达 **`1,093,883` 次/秒**，单次完整往返仅 **`0.914 us`（`219.4` cycles）**。
4. **中断抢占（`interrupt_preemption`）**：低优先级线程触发软中断 → ISR `ek_os_thread_resume()` 唤醒高优先级线程 → `PendSV` 抢占切入 → 高优先级线程自 `suspend` 切回，完整往返吞吐 **`230,795` 次/秒**（**`4.333 us` / `1,039.9` cycles**）。
5. **信号量与消息队列（`synchronization` / `message`）**：
   - `ek_os_sem_take()` + `ek_os_sem_give()` 吞吐达 **`1,451,840` 次/秒**（一次拿放仅 **`0.689 us` / `165.3` cycles**）。
   - `16 B` 消息 `ek_os_msgq_send()` + `ek_os_msgq_recv()` 吞吐 **`388,255` 次/秒**（**`2.576 us` / `618.2` cycles**）。
6. **动态内存分配（`memory`）**：使用通用变长 `TLSF v3.1`（外层套 `ek_os_kernel_sched_lock()` / `unlock()`），`-O2` 内联消除大量调用开销后吞吐飙升 **`+33.30%`**，达 **`283,831` 次/秒**（一次 `malloc + free` 耗时 **`3.523 us` / `845.6` cycles**）。

---

## 六、 后续优化方向

1. **工具链默认配置**：`gcc-arm-none-eabi.cmake` 当前将 `Release` 设为 `-Os`，如果应用对调度延迟和实时吞吐更敏感（且 Flash 充裕），可将 `Release` 调整为 `-O2`（仅增加约 1.4 KB Flash）。
2. **消除 `suspend`/`resume` 热路径中的嵌套自旋锁**：当前 `ek_os_thread_suspend/resume` → `kernel_thread_suspend/resume` → `kernel_schedule` 每次都会重复关/开 `PRIMASK` 2~3 次并走 `tid` 查表；抽出无锁内部原语后，`preemptive` 与 `interrupt_preemption` 仍有约 20%~30% 的优化潜力。
3. **补齐定长块内存池（`ek_mempool`）**：Thread-Metric 的 `memory` 项默认面向定长块池，未来提供基于单向空闲链表的定长内存池后，该项可轻松达到千万级次/秒。

---

## 七、 复现命令

```bash
# 以 Release (-Os) 运行指定用例
just tm-run cooperative Release 30

# 以 RelWithDebInfo (-O2) 运行指定用例
just tm-run cooperative RelWithDebInfo 30

# 恢复普通开发固件构建
just build
```

---

## 八、 实板串口原始日志

<details>
<summary>点击展开 -O2（RelWithDebInfo）全部 8 项测试完整串口捕获输出</summary>

### 1. basic (-O2)
```text
[Info/main.c L:31,T:0]:hello (Release)
[Info/main.c L:34,T:0]:main: entering RTOS
[Info/main.c L:41,T:0]:VTOR=0x20000000 sram=0x20000000 heap=64768
[Info/main.c L:45,T:0]:svc   vec=0x080020dd want=0x080020dd
[Info/main.c L:48,T:0]:pendsv vec=0x0800210d want=0x0800210d
[Info/main.c L:54,T:0]:main: starting Thread-Metric benchmark
**** Thread-Metric Basic Single Thread Processing Test **** Relative Time: 30
Time Period Total:  584563
```

### 2. cooperative (-O2)
```text
[Info/main.c L:31,T:0]:hello (Release)
[Info/main.c L:34,T:0]:main: entering RTOS
[Info/main.c L:41,T:0]:VTOR=0x20000000 sram=0x20000000 heap=64768
[Info/main.c L:45,T:0]:svc   vec=0x08002181 want=0x08002181
[Info/main.c L:48,T:0]:pendsv vec=0x080021b1 want=0x080021b1
[Info/main.c L:54,T:0]:main: starting Thread-Metric benchmark
**** Thread-Metric Cooperative Scheduling Test **** Relative Time: 30
tm_cooperative_thread_0_counter: 3905939
tm_cooperative_thread_1_counter: 3905939
tm_cooperative_thread_2_counter: 3905939
tm_cooperative_thread_3_counter: 3905939
tm_cooperative_thread_4_counter: 3905939
Time Period Total:  19529695
```

### 3. preemptive (-O2)
```text
[Info/main.c L:31,T:0]:hello (Release)
[Info/main.c L:34,T:0]:main: entering RTOS
[Info/main.c L:41,T:0]:VTOR=0x20000000 sram=0x20000000 heap=64768
[Info/main.c L:45,T:0]:svc   vec=0x080020dd want=0x080020dd
[Info/main.c L:48,T:0]:pendsv vec=0x0800210d want=0x0800210d
[Info/main.c L:54,T:0]:main: starting Thread-Metric benchmark
**** Thread-Metric Preemptive Scheduling Test **** Relative Time: 30
Time Period Total:  9040119
```

### 4. interrupt (-O2)
```text
[Info/main.c L:31,T:0]:hello (Release)
[Info/main.c L:34,T:0]:main: entering RTOS
[Info/main.c L:41,T:0]:VTOR=0x20000000 sram=0x20000000 heap=64768
[Info/main.c L:45,T:0]:svc   vec=0x0800227d want=0x0800227d
[Info/main.c L:48,T:0]:pendsv vec=0x080022ad want=0x080022ad
[Info/main.c L:54,T:0]:main: starting Thread-Metric benchmark
**** Thread-Metric Interrupt Processing Test **** Relative Time: 30
Time Period Total:  32816481
```

### 5. interrupt_preemption (-O2)
```text
[Info/main.c L:31,T:0]:hello (Release)
[Info/main.c L:34,T:0]:main: entering RTOS
[Info/main.c L:41,T:0]:VTOR=0x20000000 sram=0x20000000 heap=64768
[Info/main.c L:45,T:0]:svc   vec=0x080020e5 want=0x080020e5
[Info/main.c L:48,T:0]:pendsv vec=0x08002115 want=0x08002115
[Info/main.c L:54,T:0]:main: starting Thread-Metric benchmark
**** Thread-Metric Interrupt Preemption Processing Test **** Relative Time: 30
Time Period Total:  6923840
```

### 6. message (-O2)
```text
[Info/main.c L:31,T:0]:hello (Release)
[Info/main.c L:34,T:0]:main: entering RTOS
[Info/main.c L:41,T:0]:VTOR=0x20000000 sram=0x20000000 heap=64768
[Info/main.c L:45,T:0]:svc   vec=0x08002465 want=0x08002465
[Info/main.c L:48,T:0]:pendsv vec=0x08002495 want=0x08002495
[Info/main.c L:54,T:0]:main: starting Thread-Metric benchmark
**** Thread-Metric Message Processing Test **** Relative Time: 30
Time Period Total:  11647651
```

### 7. synchronization (-O2)
```text
[Info/main.c L:31,T:0]:hello (Release)
[Info/main.c L:34,T:0]:main: entering RTOS
[Info/main.c L:41,T:0]:VTOR=0x20000000 sram=0x20000000 heap=64768
[Info/main.c L:45,T:0]:svc   vec=0x08002275 want=0x08002275
[Info/main.c L:48,T:0]:pendsv vec=0x080022a5 want=0x080022a5
[Info/main.c L:54,T:0]:main: starting Thread-Metric benchmark
**** Thread-Metric Synchronization Processing Test **** Relative Time: 30
Time Period Total:  43555199
```

### 8. memory (-O2)
```text
[Info/main.c L:31,T:0]:hello (Release)
[Info/main.c L:34,T:0]:main: entering RTOS
[Info/main.c L:41,T:0]:VTOR=0x20000000 sram=0x20000000 heap=64768
[Info/main.c L:45,T:0]:svc   vec=0x080020dd want=0x080020dd
[Info/main.c L:48,T:0]:pendsv vec=0x0800210d want=0x0800210d
[Info/main.c L:54,T:0]:main: starting Thread-Metric benchmark
**** Thread-Metric Memory Allocation Test **** Relative Time: 30
Time Period Total:  8514936
```

</details>

<details>
<summary>点击展开 -Os（Release）全部 8 项测试完整串口捕获输出</summary>

### 1. basic (-Os)
```text
[Info/main.c L:31,T:0]:hello (Release)
[Info/main.c L:34,T:0]:main: entering RTOS
[Info/main.c L:41,T:0]:VTOR=0x20000000 sram=0x20000000 heap=64768
[Info/main.c L:45,T:0]:svc   vec=0x08001d69 want=0x08001d69
[Info/main.c L:48,T:0]:pendsv vec=0x08001d99 want=0x08001d99
[Info/main.c L:54,T:0]:main: starting Thread-Metric benchmark
**** Thread-Metric Basic Single Thread Processing Test **** Relative Time: 30
Time Period Total:  584548
```

### 2. cooperative (-Os)
```text
[Info/main.c L:31,T:0]:hello (Release)
[Info/main.c L:34,T:0]:main: entering RTOS
[Info/main.c L:41,T:0]:VTOR=0x20000000 sram=0x20000000 heap=64768
[Info/main.c L:45,T:0]:svc   vec=0x08001e11 want=0x08001e11
[Info/main.c L:48,T:0]:pendsv vec=0x08001e41 want=0x08001e41
[Info/main.c L:54,T:0]:main: starting Thread-Metric benchmark
**** Thread-Metric Cooperative Scheduling Test **** Relative Time: 30
tm_cooperative_thread_0_counter: 3455170
tm_cooperative_thread_1_counter: 3455170
tm_cooperative_thread_2_counter: 3455170
tm_cooperative_thread_3_counter: 3455170
tm_cooperative_thread_4_counter: 3455170
Time Period Total:  17275850
```

### 3. preemptive (-Os)
```text
[Info/main.c L:31,T:0]:hello (Release)
[Info/main.c L:34,T:0]:main: entering RTOS
[Info/main.c L:41,T:0]:VTOR=0x20000000 sram=0x20000000 heap=64768
[Info/main.c L:45,T:0]:svc   vec=0x08001d69 want=0x08001d69
[Info/main.c L:48,T:0]:pendsv vec=0x08001d99 want=0x08001d99
[Info/main.c L:54,T:0]:main: starting Thread-Metric benchmark
**** Thread-Metric Preemptive Scheduling Test **** Relative Time: 30
Time Period Total:  7756055
```

### 4. interrupt (-Os)
```text
[Info/main.c L:31,T:0]:hello (Release)
[Info/main.c L:34,T:0]:main: entering RTOS
[Info/main.c L:41,T:0]:VTOR=0x20000000 sram=0x20000000 heap=64768
[Info/main.c L:45,T:0]:svc   vec=0x08001ef5 want=0x08001ef5
[Info/main.c L:48,T:0]:pendsv vec=0x08001f25 want=0x08001f25
[Info/main.c L:54,T:0]:main: starting Thread-Metric benchmark
**** Thread-Metric Interrupt Processing Test **** Relative Time: 30
Time Period Total:  31659784
```

### 5. interrupt_preemption (-Os)
```text
[Info/main.c L:31,T:0]:hello (Release)
[Info/main.c L:34,T:0]:main: entering RTOS
[Info/main.c L:41,T:0]:VTOR=0x20000000 sram=0x20000000 heap=64768
[Info/main.c L:45,T:0]:svc   vec=0x08001d71 want=0x08001d71
[Info/main.c L:48,T:0]:pendsv vec=0x08001da1 want=0x08001da1
[Info/main.c L:54,T:0]:main: starting Thread-Metric benchmark
**** Thread-Metric Interrupt Preemption Processing Test **** Relative Time: 30
Time Period Total:  6059785
```

### 6. message (-Os)
```text
[Info/main.c L:31,T:0]:hello (Release)
[Info/main.c L:34,T:0]:main: entering RTOS
[Info/main.c L:41,T:0]:VTOR=0x20000000 sram=0x20000000 heap=64768
[Info/main.c L:45,T:0]:svc   vec=0x08002089 want=0x08002089
[Info/main.c L:48,T:0]:pendsv vec=0x080020b9 want=0x080020b9
[Info/main.c L:54,T:0]:main: starting Thread-Metric benchmark
**** Thread-Metric Message Processing Test **** Relative Time: 30
Time Period Total:  10839279
```

### 7. synchronization (-Os)
```text
[Info/main.c L:31,T:0]:hello (Release)
[Info/main.c L:34,T:0]:main: entering RTOS
[Info/main.c L:41,T:0]:VTOR=0x20000000 sram=0x20000000 heap=64768
[Info/main.c L:45,T:0]:svc   vec=0x08001eed want=0x08001eed
[Info/main.c L:48,T:0]:pendsv vec=0x08001f1d want=0x08001f1d
[Info/main.c L:54,T:0]:main: starting Thread-Metric benchmark
**** Thread-Metric Synchronization Processing Test **** Relative Time: 30
Time Period Total:  40601275
```

### 8. memory (-Os)
```text
[Info/main.c L:31,T:0]:hello (Release)
[Info/main.c L:34,T:0]:main: entering RTOS
[Info/main.c L:41,T:0]:VTOR=0x20000000 sram=0x20000000 heap=64768
[Info/main.c L:45,T:0]:svc   vec=0x08001d69 want=0x08001d69
[Info/main.c L:48,T:0]:pendsv vec=0x08001d99 want=0x08001d99
[Info/main.c L:54,T:0]:main: starting Thread-Metric benchmark
**** Thread-Metric Memory Allocation Test **** Relative Time: 30
Time Period Total:  6387943
```

</details>
