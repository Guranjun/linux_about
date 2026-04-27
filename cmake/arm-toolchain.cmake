# 设置目标系统名
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# --- 请根据你的实际路径修改以下两行 ---
# 这是 Buildroot 生成的 host 工具目录
set(BR_PATH "/home/guranjun/100ask_imx6ull-sdk/Buildroot_2020.02.x")

# 这是包含库和头文件的 sysroot 目录
set(CMAKE_FIND_ROOT_PATH "${BR_PATH}/output/host/arm-buildroot-linux-gnueabihf/sysroot")

# 指定编译器
set(CMAKE_C_COMPILER "${BR_PATH}/output/host/bin/arm-buildroot-linux-gnueabihf-gcc")
set(CMAKE_CXX_COMPILER "${BR_PATH}/output/host/bin/arm-buildroot-linux-gnueabihf-g++")


# 设置寻找策略：头文件和库去 sysroot 找，程序去宿主机找
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(CMAKE_C_FLAGS "-mcpu=cortex-a7 -mfloat-abi=hard -mfpu=vfpv4-d16" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS}" CACHE STRING "" FORCE)