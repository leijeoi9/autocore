# ============================================================
# CMake Toolchain File for I.MX6ull (ARM Cortex-A7)
#
# 使用方法:
#   cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/arm-linux-gnueabihf.cmake ..
#   make -j$(nproc)
#
# 交叉编译器路径: /usr/local/arm/gcc-linaro-4.9.4-2017.01-x86_64_arm-linux-gnueabihf/
# ============================================================

# 目标系统
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# 编译器
set(CMAKE_C_COMPILER   arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)

# 查找工具链的路径
set(CMAKE_FIND_ROOT_PATH /usr/local/arm/gcc-linaro-4.9.4-2017.01-x86_64_arm-linux-gnueabihf)

# 只在目标系统上查找库和头文件
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# 编译选项 — 针对 Cortex-A7 优化
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=armv7ve -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard")
