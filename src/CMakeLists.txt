cmake_minimum_required(VERSION 3.10)

project(Monitor)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 11)
# 1. 产物输出到 bin
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../bin)

# 2. 寻找库 (得益于 toolchain 中的 sysroot，它会自动找到图中的那些 .so)
find_package(OpenCV REQUIRED)
find_package(PkgConfig REQUIRED)

# 3. 头文件包含
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../inc
    ${OpenCV_INCLUDE_DIRS}
)

# 4. 搜集所有源文件
file(GLOB_RECURSE SRCS "*.c" "*.cpp")

# 5. 生成目标
add_executable(video_detect ${SRCS})

# 6. 链接“全家桶”
target_link_libraries(video_target 
    PRIVATE 
    ${OpenCV_LIBS} 
    pthread m dl
)