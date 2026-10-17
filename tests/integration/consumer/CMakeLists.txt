cmake_minimum_required(VERSION 3.20)
project(SoftRendererConsumer LANGUAGES CXX)

if(SR_MODE STREQUAL "subdirectory")
    # 故意不关闭示例/测试：验证作为依赖引入时默认不会污染父工程。
    add_subdirectory("${SR_SOURCE_DIR}" softrenderer)
    foreach(unexpected IN ITEMS unit_tests render_regression stage01_framebuffer practice_hello_2d mylog)
        if(TARGET ${unexpected})
            message(FATAL_ERROR "Unexpected dependency target: ${unexpected}")
        endif()
    endforeach()
else()
    find_package(SoftRenderer 1.0 CONFIG REQUIRED)
endif()

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE SoftRenderer::softrenderer)
enable_testing()
add_test(NAME consumer_runs COMMAND consumer)
