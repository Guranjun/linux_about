cmake_minimum_required(VERSION 3.10)
project(Monitor)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 11)

# --- 修改 1: 输出目录 ---
# 既然已经在根目录了，输出到 bin 就不需要 ../ 了，直接用 bin
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)

# 2. 寻找库
find_package(OpenCV REQUIRED)

# --- 修改 2: 头文件包含 ---
# ${CMAKE_CURRENT_SOURCE_DIR} 现在就是 video 目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
    ${OpenCV_INCLUDE_DIRS}
)

# --- 修改 3: 搜集源文件 ---
# 告诉 CMake 去 src 子目录下找文件
file(GLOB_RECURSE SRCS "src/*.c" "src/*.cpp")

# 5. 生成目标
add_executable(video_detect ${SRCS})

# 6. 链接库 (保持不变，但记得检查目标名称一致)
target_link_libraries(video_detect 
    PRIVATE 
    ${OpenCV_LIBS} 
    pthread m dl
)