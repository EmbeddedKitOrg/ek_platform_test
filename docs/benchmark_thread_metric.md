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
| **构建模式** | `RelWithDebInfo`（`-O2 -g3 -DNDEBUG`） |
| **内核时基** | Tickless 无滴答调度：`DWT->CYCCNT` 自由计时 + `TIMER5` 单次闹钟（`1 MHz`，`1 tick = 1 us`） |
| **堆与内存布局** | `TLSF v3.1` 位于 `64 KB TCMRAM`（自动推导 `EKCFG_TLSF_FL_INDEX_MAX = 16`、`EKCFG_TLSF_SL_INDEX_COUNT_LOG2 = 4`） |
| **基准测试套件** | Eclipse ThreadX Thread-Metric `v6.1.7`（标准 `TM_TEST_DURATION = 30 s`） |
| **测试线程栈** | `1024 B` / 线程；消息队列深度 `10`（单条 `16 B`）；内存测试池 `4096 B`（分配 `128 B` 块） |
| **软件中断源** | `CAN1_TX_IRQn`（IRQ 63，NVIC `PendSV` 最低优先级 `0xFF`，`TM_SW_IRQn` 优先级 `(0, 2)`） |
| **烧录与采样** | DAPLink CMSIS-DAP（`probe-rs`）+ 串口 `/dev/ttyACM0`（`115200 8N1`） |

---

## 二、 核心结果汇总（`-O2` / 30 秒标准周期）

| 用例 | 说明 | `30s` 总分 | 吞吐率 (`ops/s`) | 单次耗时 (`us`) | 单次周期 (`cycles`) | `FLASH` / `RAM` (`TCMRAM=64 KB`) |
| :--- | :--- | ---: | ---: | ---: | ---: | :--- |
| **basic** | 单线程运算基线 | `585,632` | `19,521.07` | `51.227` | `12,294.4` | `92,308 B` / `10,048 B` |
| **cooperative** | 5 线程协作式调度 | `24,572,445` | `819,081.50` | `1.221` | `293.0` | `92,992 B` / `5,968 B` |
| **preemptive** | 5 优先级抢占式调度 | `11,896,460` | `396,548.67` | `2.522` | `605.2` | `92,600 B` / `5,968 B` |
| **interrupt** | 中断响应与信号量通知 | `32,875,690` | `1,095,856.33` | `0.913` | `219.0` | `92,980 B` / `5,976 B` |
| **interrupt_preemption** | 中断唤醒高优先级抢占 | `8,988,444` | `299,614.80` | `3.338` | `801.0` | `92,464 B` / `5,960 B` |
| **message** | 16 B 消息收发 | `12,223,726` | `407,457.53` | `2.454` | `589.0` | `93,452 B` / `6,008 B` |
| **synchronization** | 信号量拿放同步 | `43,635,003` | `1,454,500.10` | `0.688` | `165.0` | `92,884 B` / `5,968 B` |
| **memory** | 128 B 内存池分配释放 | `8,490,300` | `283,010.00` | `3.533` | `848.0` | `92,464 B` / `22,360 B` |

---

## 三、 `-O2`（`RelWithDebInfo`）全量明细

| 用例 | 对应测试文件 | `Time Period Total (30s)` | 吞吐率 (`ops/s`) | 单次计数耗时 (`us`) | 单次计数周期 (`cycles`) | 墙钟实测 (`s`) | `FLASH` / `RAM` (`TCMRAM=64 KB`) | 自检状态 |
| :--- | :--- | ---: | ---: | ---: | ---: | ---: | :--- | :---: |
| **basic** | `tm_basic_processing_test.c` | `585,632` | `19,521.07` | `51.227` | `12,294.4` | `30.542` | `92,308 B` / `10,048 B` | PASS |
| **cooperative** | `tm_cooperative_scheduling_test.c` | `24,572,445` | `819,081.50` | `1.221` | `293.0` | `30.541` | `92,992 B` / `5,968 B` | PASS |
| **preemptive** | `tm_preemptive_scheduling_test.c` | `11,896,460` | `396,548.67` | `2.522` | `605.2` | `30.348` | `92,600 B` / `5,968 B` | PASS |
| **interrupt** | `tm_interrupt_processing_test.c` | `32,875,690` | `1,095,856.33` | `0.913` | `219.0` | `30.539` | `92,980 B` / `5,976 B` | PASS |
| **interrupt_preemption** | `tm_interrupt_preemption_processing_test.c` | `8,988,444` | `299,614.80` | `3.338` | `801.0` | `30.537` | `92,464 B` / `5,960 B` | PASS |
| **message** | `tm_message_processing_test.c` | `12,223,726` | `407,457.53` | `2.454` | `589.0` | `30.539` | `93,452 B` / `6,008 B` | PASS |
| **synchronization** | `tm_synchronization_processing_test.c` | `43,635,003` | `1,454,500.10` | `0.688` | `165.0` | `30.535` | `92,884 B` / `5,968 B` | PASS |
| **memory** | `tm_memory_allocation_test.c` | `8,490,300` | `283,010.00` | `3.533` | `848.0` | `30.540` | `92,464 B` / `22,360 B` | PASS |

> 注：`TCMRAM` 占用显示 `64 KB` 是因为链接阶段为默认堆数组静态分配了整块 `TCMRAM` 空间；全量 8 项 30 秒测试均无 `ERROR` 日志，各多任务测试均通过官方计数器校验。

---

## 四、 分项性能解读

1. **协作式切换（`cooperative`）**：5 个同优先级线程依次 `ek_os_thread_yield()`，30 秒总计 **24,572,445** 次切换（**819,081.50 次/秒**），5 个线程精准各得 **`4,914,489`** 次，调度完全均衡；单次完整让出切换仅 **`1.221 us`（`293.0` cycles）**。
2. **抢占式切换（`preemptive`）**：5 个不同优先级线程通过 `resume` 级联抢占再逐级自 `suspend` 返回。一轮包含 5 次计数增和 8 次上下文切换（`0→1→2→3→4→3→2→1→0`），折合抢占切换吞吐率为 **`634,477.87` 次/秒**，平均单次含 API 查找与 `PendSV` 的抢占切换约 **`1.576 us`（`378.3` cycles）**。
3. **中断响应（`interrupt`）**：软件触发 `CAN1_TX_IRQn` → ISR `ek_os_sem_give()` → 线程 `ek_os_sem_take()`，吞吐达 **`1,095,856.33` 次/秒**，单次完整往返仅 **`0.913 us`（`219.0` cycles）**。
4. **中断抢占（`interrupt_preemption`）**：低优先级线程触发软中断 → ISR `ek_os_thread_resume()` 唤醒高优先级线程 → `PendSV` 抢占切入 → 高优先级线程自 `suspend` 切回，完整往返吞吐 **`299,614.80` 次/秒**（**`3.338 us` / `801.0` cycles**）。
5. **信号量与消息队列（`synchronization` / `message`）**：
   - `ek_os_sem_take()` + `ek_os_sem_give()` 吞吐达 **`1,454,500.10` 次/秒**（一次拿放仅 **`0.688 us` / `165.0` cycles**）。
   - `16 B` 消息 `ek_os_msgq_send()` + `ek_os_msgq_recv()` 吞吐 **`407,457.53` 次/秒**（**`2.454 us` / `589.0` cycles**）。
6. **动态内存分配（`memory`）**：使用通用变长 `TLSF v3.1`（外层套 `ek_os_kernel_sched_lock()` / `unlock()`），吞吐达 **`283,010.00` 次/秒**（一次 `malloc + free` 耗时 **`3.533 us` / `848.0` cycles**）。

---

## 五、 后续优化方向

1. **工具链默认配置**：`gcc-arm-none-eabi.cmake` 当前将 `Release` 设为 `-Os`，如应用对调度延迟与实时吞吐更敏感（且 Flash 充裕），可将 `Release` 调整为 `-O2`。
2. **补齐定长块内存池（`ek_mempool`）**：Thread-Metric 的 `memory` 项面向定长块池，当前走通用变长 `TLSF`，后续提供基于单向空闲链表的定长块池可进一步大幅提高定长分配吞吐。

---

## 六、 复现命令

```bash
# 以 RelWithDebInfo (-O2) 运行指定用例（默认 30s）
just tm-run cooperative RelWithDebInfo 30

# 以 Release (-Os) 运行指定用例
just tm-run cooperative Release 30

# 恢复默认 Debug 开发固件构建
just build
```

---

## 七、 实板串口原始日志

<details>
<summary>点击展开 -O2（RelWithDebInfo）全部 8 项测试完整串口捕获输出</summary>

### 1. basic (-O2)
```text
[Info/main.c L:31,T:0]:hello (Release)
[Info/main.c L:34,T:0]:main: entering RTOS
[Info/main.c L:41,T:0]:VTOR=0x20000000 sram=0x20000000 heap=64768
[Info/main.c L:45,T:0]:svc   vec=0x08001fad want=0x08001fad
[Info/main.c L:48,T:0]:pendsv vec=0x08001fdd want=0x08001fdd
[Info/main.c L:54,T:0]:main: starting Thread-Metric benchmark
**** Thread-Metric Basic Single Thread Processing Test **** Relative Time: 30
Time Period Total:  585632
```

### 2. cooperative (-O2)
```text
[Info/main.c L:31,T:0]:hello (Release)
[Info/main.c L:34,T:0]:main: entering RTOS
[Info/main.c L:41,T:0]:VTOR=0x20000000 sram=0x20000000 heap=64768
[Info/main.c L:45,T:0]:svc   vec=0x0800203d want=0x0800203d
[Info/main.c L:48,T:0]:pendsv vec=0x0800206d want=0x0800206d
[Info/main.c L:54,T:0]:main: starting Thread-Metric benchmark
**** Thread-Metric Cooperative Scheduling Test **** Relative Time: 30
tm_cooperative_thread_0_counter: 4914489
tm_cooperative_thread_1_counter: 4914489
tm_cooperative_thread_2_counter: 4914489
tm_cooperative_thread_3_counter: 4914489
tm_cooperative_thread_4_counter: 4914489
Time Period Total:  24572445
```

### 3. preemptive (-O2)
```text
[Info/main.c L:31,T:0]:hello (Release)
[Info/main.c L:34,T:0]:main: entering RTOS
[Info/main.c L:41,T:0]:VTOR=0x20000000 sram=0x20000000 heap=64768
[Info/main.c L:45,T:0]:svc   vec=0x08001fad want=0x08001fad
[Info/main.c L:48,T:0]:pendsv vec=0x08001fdd want=0x08001fdd
[Info/main.c L:54,T:0]:main: starting Thread-Metric benchmark
**** Thread-Metric Preemptive Scheduling Test **** Relative Time: 30
Time Period Total:  11896460
```

### 4. interrupt (-O2)
```text
[Info/main.c L:31,T:0]:hello (Release)
[Info/main.c L:34,T:0]:main: entering RTOS
[Info/main.c L:41,T:0]:VTOR=0x20000000 sram=0x20000000 heap=64768
[Info/main.c L:45,T:0]:svc   vec=0x08002195 want=0x08002195
[Info/main.c L:48,T:0]:pendsv vec=0x080021c5 want=0x080021c5
[Info/main.c L:54,T:0]:main: starting Thread-Metric benchmark
**** Thread-Metric Interrupt Processing Test **** Relative Time: 30
Time Period Total:  32875690
```

### 5. interrupt_preemption (-O2)
```text
[Info/main.c L:31,T:0]:hello (Release)
[Info/main.c L:34,T:0]:main: entering RTOS
[Info/main.c L:41,T:0]:VTOR=0x20000000 sram=0x20000000 heap=64768
[Info/main.c L:45,T:0]:svc   vec=0x08001fb5 want=0x08001fb5
[Info/main.c L:48,T:0]:pendsv vec=0x08001fe5 want=0x08001fe5
[Info/main.c L:54,T:0]:main: starting Thread-Metric benchmark
**** Thread-Metric Interrupt Preemption Processing Test **** Relative Time: 30
Time Period Total:  8988444
```

### 6. message (-O2)
```text
[Info/main.c L:31,T:0]:hello (Release)
[Info/main.c L:34,T:0]:main: entering RTOS
[Info/main.c L:41,T:0]:VTOR=0x20000000 sram=0x20000000 heap=64768
[Info/main.c L:45,T:0]:svc   vec=0x0800237d want=0x0800237d
[Info/main.c L:48,T:0]:pendsv vec=0x080023ad want=0x080023ad
[Info/main.c L:54,T:0]:main: starting Thread-Metric benchmark
**** Thread-Metric Message Processing Test **** Relative Time: 30
Time Period Total:  12223726
```

### 7. synchronization (-O2)
```text
[Info/main.c L:31,T:0]:hello (Release)
[Info/main.c L:34,T:0]:main: entering RTOS
[Info/main.c L:41,T:0]:VTOR=0x20000000 sram=0x20000000 heap=64768
[Info/main.c L:45,T:0]:svc   vec=0x0800218d want=0x0800218d
[Info/main.c L:48,T:0]:pendsv vec=0x080021bd want=0x080021bd
[Info/main.c L:54,T:0]:main: starting Thread-Metric benchmark
**** Thread-Metric Synchronization Processing Test **** Relative Time: 30
Time Period Total:  43635003
```

### 8. memory (-O2)
```text
[Info/main.c L:31,T:0]:hello (Release)
[Info/main.c L:34,T:0]:main: entering RTOS
[Info/main.c L:41,T:0]:VTOR=0x20000000 sram=0x20000000 heap=64768
[Info/main.c L:45,T:0]:svc   vec=0x08001fad want=0x08001fad
[Info/main.c L:48,T:0]:pendsv vec=0x08001fdd want=0x08001fdd
[Info/main.c L:54,T:0]:main: starting Thread-Metric benchmark
**** Thread-Metric Memory Allocation Test **** Relative Time: 30
Time Period Total:  8490300
```

</details>
