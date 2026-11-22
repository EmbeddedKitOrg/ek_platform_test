# ek_platform_gd32f470

面向 **GD32F470ZGT6**（ARM Cortex-M4F @ 240 MHz）的 `ek_platform` 无滴答 RTOS 平台、板级驱动与内核性能基准验证工程。

---

## 一、 测试环境与内核配置

| 项目 | 配置详情 |
| :--- | :--- |
| **目标芯片** | GD32F470ZGT6（ARM Cortex-M4F，硬件单精度浮点 `fpv4-sp-d16`） |
| **系统主频** | `240 MHz`（25 MHz HXTAL PLL 倍频） |
| **工具链** | `arm-none-eabi-gcc 14.2.1` + `picolibc` |
| **基准测试构建模式** | `RelWithDebInfo`（`-O2 -g3 -DNDEBUG`） |
| **内核时基** | Tickless 无滴答调度：`DWT->CYCCNT` 自由计时 + `TIMER5` 单次闹钟（`1 MHz`，`1 tick = 1 us`） |
| **内存布局与堆** | `FLASH 1 MB` / `RAM 448 KB` / `TCMRAM 64 KB`（`TLSF v3.1` 默认堆） |
| **调试与串口** | DAPLink CMSIS-DAP（`probe-rs`）+ `/dev/ttyACM0`（`115200 8N1`） |

---

## 二、 目录结构与常用命令

| 目录 / 文件 | 用途 |
| :--- | :--- |
| `src/` / `include/` | 应用入口、中断处理、板级 BSP（`bsp_led` / `bsp_uart` / `bsp_os_timer` 等）及功能自检（`src/test/`） |
| `lib/ek_platform/` | `ek_platform` 子模块（Tickless RTOS 内核、数据结构、TLSF 堆、VFS、Shell、Cortex-M4 移植层） |
| `lib/gd32f4xx_driver/` | GD32F4xx 标准外设库与 CMSIS 支持 |
| `bench/thread_metric/` | Eclipse ThreadX Thread-Metric `v6.1.7` 移植层 |
| `bench/latency_measure/` | Zephyr `latency_measure` 风格周期级微基准测试套件（基于 `DWT->CYCCNT`） |
| `docs/` | 工程文档与详细基准测试报告 |

常用构建与基准测试命令（基于 `justfile`）：

```bash
# 构建、烧录与串口监控默认固件（Debug，开启 os_test）
just build
just flash
just monitor

# 运行 Thread-Metric 单项基准（RelWithDebInfo / 30 秒）
just tm-run cooperative RelWithDebInfo 30

# 运行 Zephyr latency_measure 风格微基准（RelWithDebInfo / 每项 1000 次采样）
just lm-run RelWithDebInfo 1000
```

---

## 三、 Thread-Metric v6.1.7 基准测试（30 秒标准吞吐）

在 `-O2`（`RelWithDebInfo`）、`240 MHz` 实机下运行标准 `TM_TEST_DURATION = 30 s` 全套 8 项基准，各多任务计数器均保持均衡且全程零错误：

| 用例 | 测试内容 | `30s` 总分 | 吞吐率 (`ops/s`) | 单次耗时 (`us`) | 单次周期 (`cycles`) | `FLASH` / `RAM` (`TCMRAM=64 KB`) |
| :--- | :--- | ---: | ---: | ---: | ---: | :--- |
| **basic** | 单线程运算基线 | `585,632` | `19,521.07` | `51.227` | `12,294.4` | `92,308 B` / `10,048 B` |
| **cooperative** | 5 线程协作式让出（`yield`） | `24,572,445` | `819,081.50` | `1.221` | `293.0` | `92,992 B` / `5,968 B` |
| **preemptive** | 5 优先级抢占切换与挂起恢复 | `11,896,460` | `396,548.67` | `2.522` | `605.2` | `92,600 B` / `5,968 B` |
| **interrupt** | 软中断响应与信号量通知 | `32,875,690` | `1,095,856.33` | `0.913` | `219.0` | `92,980 B` / `5,976 B` |
| **interrupt_preemption** | 中断唤醒高优先级线程并抢占切换 | `8,988,444` | `299,614.80` | `3.338` | `801.0` | `92,464 B` / `5,960 B` |
| **message** | 16 B 消息队列收发 | `12,223,726` | `407,457.53` | `2.454` | `589.0` | `93,452 B` / `6,008 B` |
| **synchronization** | 无竞争二值/计数信号量拿放 | `43,635,003` | `1,454,500.10` | `0.688` | `165.0` | `92,884 B` / `5,968 B` |
| **memory** | 128 B 内存分配释放（通用 `TLSF v3.1`） | `8,490,300` | `283,010.00` | `3.533` | `848.0` | `92,464 B` / `22,360 B` |

- **协作式调度均衡性**：`cooperative` 5 个线程在 30 秒内均精准完成 `4,914,489` 次让出计数。
- **抢占式单次切换折算**：`preemptive` 每轮包含 5 次计数和 8 次上下文切换，折合单次抢占上下文切换吞吐率为 **`634,477.87 ops/s`**，单次耗时 **`1.576 us`**（**`378.3 cycles`**）。
- 完整明细、参数说明与实板串口原始输出见 [Thread-Metric 基准测试报告](docs/benchmark_thread_metric.md)。

---

## 四、 Zephyr `latency_measure` 风格微基准（周期级延迟）

基于 ARM `DWT->CYCCNT` 实现的高精度周期级微基准，测试参数如下：

- **构建模式**：`RelWithDebInfo`（`-O2`），固件占用 `FLASH = 102,032 B`、`RAM = 7,440 B`、`TCMRAM = 64 KB`
- **主频与采样**：`240 MHz`（`1 cycle ≈ 4.167 ns`），单项采样 `1000` 次（含 `16` 次预热），实测读时间戳校准开销 `ts_overhead = 1 cyc`（已自动扣除）
- **软中断源**：`CAN1_TX_IRQn`（IRQ 63）

### 4.1 线程调度与生命周期

| Benchmark | avg (`cyc`) | min (`cyc`) | max (`cyc`) | avg (`ns`) | max (`ns`) |
| :--- | ---: | ---: | ---: | ---: | ---: |
| `thread.yield (2 threads)` | `287` | `287` | `287` | `1,196` | `1,196` |
| `thread.suspend (other)` | `182` | `182` | `182` | `758` | `758` |
| `thread.resume (other)` | `176` | `176` | `176` | `733` | `733` |
| `thread.resume -> preempt` | `399` | `399` | `399` | `1,663` | `1,663` |
| `thread.create+join (static)` | `2,451` | `2,451` | `2,451` | `10,213` | `10,213` |
| `thread.create+join (dynamic)` | `3,275` | `3,275` | `3,275` | `13,646` | `13,646` |

### 4.2 中断入口与返回路径

| Benchmark | avg (`cyc`) | min (`cyc`) | max (`cyc`) | avg (`ns`) | max (`ns`) |
| :--- | ---: | ---: | ---: | ---: | ---: |
| `isr.entry` | `36` | `36` | `36` | `150` | `150` |
| `isr.to_interrupted_thread` | `27` | `27` | `27` | `113` | `113` |
| `isr.to_preempt_thread (sem)` | `449` | `449` | `449` | `1,871` | `1,871` |

### 4.3 信号量、互斥锁、条件变量与屏障

| Benchmark | avg (`cyc`) | min (`cyc`) | max (`cyc`) | avg (`ns`) | max (`ns`) |
| :--- | ---: | ---: | ---: | ---: | ---: |
| `sem.give (no waiter)` | `54` | `54` | `54` | `225` | `225` |
| `sem.take (no block)` | `65` | `65` | `65` | `271` | `271` |
| `sem.give -> wakeup switch` | `442` | `442` | `442` | `1,842` | `1,842` |
| `mutex.lock (uncontended)` | `108` | `108` | `108` | `450` | `450` |
| `mutex.unlock (uncontended)` | `194` | `194` | `194` | `808` | `808` |
| `mutex.relock+unlock (recursive)` | `162` | `162` | `162` | `675` | `675` |
| `mutex.unlock -> handoff (PI)` | `538` | `538` | `538` | `2,242` | `2,242` |
| `condvar.signal (with waiter)` | `225` | `225` | `225` | `938` | `938` |
| `condvar.unlock -> wakeup+reacq` | `572` | `572` | `572` | `2,383` | `2,383` |
| `barrier.wait (expected=1)` | `139` | `139` | `139` | `579` | `579` |
| `barrier.wait (2 thr release)` | `526` | `526` | `526` | `2,192` | `2,192` |

### 4.4 无锁风格队列、消息队列与堆分配

| Benchmark | avg (`cyc`) | min (`cyc`) | max (`cyc`) | avg (`ns`) | max (`ns`) |
| :--- | ---: | ---: | ---: | ---: | ---: |
| `kfifo.put (no waiter)` | `64` | `64` | `64` | `267` | `267` |
| `kfifo.peek` | `43` | `43` | `43` | `179` | `179` |
| `kfifo.get (no block)` | `83` | `83` | `83` | `346` | `346` |
| `kfifo.put -> direct handoff` | `456` | `456` | `456` | `1,900` | `1,900` |
| `msgq.send (16B, no waiter)` | `254` | `254` | `254` | `1,058` | `1,058` |
| `msgq.recv (16B, no block)` | `253` | `253` | `253` | `1,054` | `1,054` |
| `msgq.send (16B) -> wakeup switch` | `800` | `800` | `800` | `3,333` | `3,333` |
| `heap.malloc (128B)` | `473` | `473` | `473` | `1,971` | `1,971` |
| `heap.free (128B)` | `362` | `362` | `362` | `1,508` | `1,508` |
