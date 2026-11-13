alias c := clean
alias b := build
alias f := flash
alias m := monitor
alias r := run
alias s := sim
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
    probe-rs reset --chip GD32F470ZG
    tio -b 115200 /dev/ttyACM0 

# Renode 仿真：直接加载 build/main.elf，终端只显示 UART0 输出，无任何 Renode 日志
sim:build
    renode --plain --disable-gui -P -1 --hide-log scripts/renode/run.resc

# Renode 仿真 + Monitor 端口 + GDB：可发命令、可调试
#   发命令: nc localhost 1234  →  usart0 WriteLine "help"
#   GDB  : arm-none-eabi-gdb build/main.elf -ex "target remote :3333"
sim-debug:build
    renode --plain --disable-gui --hide-log -e "machine StartGdbServer 3333" scripts/renode/run.resc

format:
    fd -e c -e h . src include lib -x clang-format -i
    
run:build flash monitor
