alias c := clean
alias b := build
alias f := flash
alias m := monitor
alias r := run
alias s := sim
alias st := sim-term
alias sd := sim-debug
alias fmt := format

clean:
    rm -rf build

build mode="Debug":
    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE={{mode}} -DEK_BENCH_TM=OFF
    ninja -C build

# 构建 Thread-Metric 基准测试（case: basic|cooperative|preemptive|interrupt|interrupt_preemption|message|synchronization|memory）
tm case="basic" mode="Release" duration="30":
    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE={{mode}} -DEK_BENCH_TM={{case}} -DEK_BENCH_TM_DURATION={{duration}}
    ninja -C build

tm-flash case="basic" mode="Release" duration="30": (tm case mode duration)
    probe-rs download --speed 4000 --chip GD32F470ZG ./build/main.elf
    probe-rs reset --chip GD32F470ZG

tm-run case="basic" mode="Release" duration="30": (tm-flash case mode duration)
    clear
    probe-rs reset --chip GD32F470ZG
    tio -b 115200 /dev/ttyACM0

tm-sim case="basic" mode="Release" duration="5": (tm case mode duration)
    renode --plain --disable-gui -P -1 --hide-log scripts/renode/run_term.resc

flash:build
    probe-rs download --speed 4000 --chip GD32F470ZG ./build/main.elf
    probe-rs reset --chip GD32F470ZG
        
monitor:
    clear
    probe-rs reset --chip GD32F470ZG
    tio -b 115200 /dev/ttyACM0 

# Renode 仿真（GUI）：弹出 UART0 串口窗口，可直接敲键盘
sim:build
    renode scripts/renode/run.resc

# Renode 仿真（纯终端）：UART0 输出直接打印在本终端，不能输入
sim-term:build
    renode --plain --disable-gui -P -1 --hide-log scripts/renode/run_term.resc

# Renode 仿真 + Monitor + GDB：通过 nc 发命令，可调试
#   发命令: nc localhost 1234  →  usart0 WriteLine "help"
#   GDB  : arm-none-eabi-gdb build/main.elf -ex "target remote :3333"
sim-debug:build
    renode --plain --disable-gui --hide-log -e "machine StartGdbServer 3333" scripts/renode/run_term.resc

format:
    fd -e c -e h . src include lib -x clang-format -i
    
run:build flash monitor
