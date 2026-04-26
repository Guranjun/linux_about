#!/bin/bash
mkdir -p build && cd build

# 引用你写的那个工具链文件
cmake ../src -DCMAKE_TOOLCHAIN_FILE=../cmake/arm-toolchain.cmake
make -j$(nproc)