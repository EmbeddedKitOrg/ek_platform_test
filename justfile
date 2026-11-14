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

build:
    cmake -B build -G Ninja
    ninja -C build

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
