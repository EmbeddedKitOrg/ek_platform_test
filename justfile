alias c := clean
alias b := build
alias f := flash
alias m := monitor
alias r := run
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
format:
    fd -e c -e h . src include lib -x clang-format -i
    
run:build flash monitor
