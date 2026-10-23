cmake_minimum_required(VERSION 3.20)
project(SoftRendererCoreConsumer LANGUAGES CXX)

# 只请求 core：验证 find_package(SoftRenderer COMPONENTS core) 的依赖闭包
# 不引入 io / runtime / Threads。
if(SR_MODE STREQUAL "subdirectory")
    add_subdirectory("${SR_SOURCE_DIR}" softrenderer_core_dep)
else()
    find_package(SoftRenderer 2.0 CONFIG REQUIRED COMPONENTS core)
endif()

add_executable(consumer_core main.cpp)
target_link_libraries(consumer_core PRIVATE SoftRenderer::core)
enable_testing()
add_test(NAME consumer_core_runs COMMAND consumer_core)
