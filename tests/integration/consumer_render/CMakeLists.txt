cmake_minimum_required(VERSION 3.20)
project(SoftRendererRenderConsumer LANGUAGES CXX)

# 只请求 render：验证 core+math+resource+render 的依赖闭包不拉入
# io / runtime / Threads / platform。
if(SR_MODE STREQUAL "subdirectory")
    add_subdirectory("${SR_SOURCE_DIR}" softrenderer_render_dep)
else()
    find_package(SoftRenderer 2.0 CONFIG REQUIRED COMPONENTS render)
endif()

add_executable(consumer_render main.cpp)
target_link_libraries(consumer_render PRIVATE SoftRenderer::render)
enable_testing()
add_test(NAME consumer_render_runs COMMAND consumer_render)
