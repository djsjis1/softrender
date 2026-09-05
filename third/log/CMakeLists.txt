# mylog - spdlog 轻量封装
# 使用方式（在上层 CMake 中）：
#   add_subdirectory(path/to/log)
#   target_link_libraries(your_target PRIVATE mylog)
# 业务代码只需: #include "Logger.h" 并使用 LOG_INFO(...) 等宏。

cmake_minimum_required(VERSION 3.11)

# 允许独立构建，也允许被 add_subdirectory 引入
if(NOT DEFINED PROJECT_NAME)
    project(mylog CXX)
endif()

# ------------------ 选项 ------------------
option(MYLOG_BUILD_EXAMPLE "Build mylog example" OFF)

# ------------------ 依赖: spdlog ------------------
# 如果外层已经 add_subdirectory 了 spdlog，则复用；否则自动引入本目录下的 spdlog 源码
if(NOT TARGET spdlog::spdlog AND NOT TARGET spdlog)
    # 关闭 spdlog 自己的示例/测试，减少构建负担
    set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
    set(SPDLOG_BUILD_TESTS   OFF CACHE BOOL "" FORCE)
    set(SPDLOG_BUILD_BENCH   OFF CACHE BOOL "" FORCE)
    set(SPDLOG_INSTALL       OFF CACHE BOOL "" FORCE)

    add_subdirectory(
        ${CMAKE_CURRENT_SOURCE_DIR}/spdlog-1.17.0
        ${CMAKE_CURRENT_BINARY_DIR}/spdlog-build
        EXCLUDE_FROM_ALL
    )
endif()

# ------------------ 封装静态库 ------------------
add_library(mylog STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Logger.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Logger.h
)

# 给上层暴露 C++17 要求
target_compile_features(mylog PUBLIC cxx_std_17)

# 头文件搜索路径对下游可见
target_include_directories(mylog
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# 链接 spdlog（兼容两种 target 名字）
if(TARGET spdlog::spdlog)
    target_link_libraries(mylog PUBLIC spdlog::spdlog)
elseif(TARGET spdlog)
    target_link_libraries(mylog PUBLIC spdlog)
endif()

# 打开编译期最低日志级别（影响 SPDLOG_LOGGER_XXX 宏；这里放低到 TRACE）
target_compile_definitions(mylog PUBLIC SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)

# 方便以别名方式引用
add_library(mylog::mylog ALIAS mylog)

# ------------------ 示例（可选） ------------------
if(MYLOG_BUILD_EXAMPLE)
    add_executable(mylog_example ${CMAKE_CURRENT_SOURCE_DIR}/example.cpp)
    target_link_libraries(mylog_example PRIVATE mylog)
endif()
