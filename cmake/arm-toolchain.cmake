set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(SDK_PATH /home/guranjun/100ask_imx6ull-sdk/ToolChain/arm-buildroot-linux-gnueabihf_sdk-buildroot)


# 直接写编译器名字，CMake 会自动在环境变量 PATH 中搜索
set(CMAKE_C_COMPILER arm-buildroot-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER arm-buildroot-linux-gnueabihf-g++)

set(CMAKE_SYSROOT ${SDK_PATH}/arm-buildroot-linux-gnueabihf/sysroot)
# 这一行非常重要，防止 CMake 跑去链接宿主机的库
set(CMAKE_FIND_ROOT_PATH ${CMAKE_SYSROOT})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)