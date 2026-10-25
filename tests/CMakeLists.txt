# ============================================================
# 架构门禁
#   1. 每个公共头单独编译（不依赖 include 顺序）
#   2. 命名空间冲突哨兵
#   3. 模块 include 白名单（render 不可见 io/runtime/scene；core 无文件/日志/线程）
#
# 这三项只依赖公共头，与可选组件开关无关，因此必须放在下面的
# 「关闭 io/runtime 就整体 return」之前：否则裁剪构建下核心库与
# 命名空间约定完全失去校验。
# ============================================================
file(GLOB_RECURSE SR_PUBLIC_HEADERS RELATIVE "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/include/softrenderer/*.h")
set(SR_HEADER_TUS "")
foreach(header IN LISTS SR_PUBLIC_HEADERS)
    string(MAKE_C_IDENTIFIER "${header}" header_id)
    set(tu "${CMAKE_CURRENT_BINARY_DIR}/generated/header_${header_id}.cpp")
    file(WRITE "${tu}" "#include <${header}>\nvoid sr_header_${header_id}() {}\n")
    list(APPEND SR_HEADER_TUS "${tu}")
endforeach()
add_library(public_headers_check OBJECT ${SR_HEADER_TUS})
target_link_libraries(public_headers_check PRIVATE SoftRenderer::softrenderer sr_dev_flags)

add_library(namespace_sentinel OBJECT architecture/namespace_sentinel.cpp)
target_link_libraries(namespace_sentinel PRIVATE SoftRenderer::softrenderer sr_dev_flags)

enable_testing()
add_test(NAME architecture_boundaries
    COMMAND "${CMAKE_COMMAND}" "-DSR_INCLUDE_DIR=${PROJECT_SOURCE_DIR}/include"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/architecture/check_boundaries.cmake")
set_tests_properties(architecture_boundaries PROPERTIES LABELS "packaging" TIMEOUT 60)

# 单元测试与像素回归需要 io（加载真实文件）与 runtime（线程池/缓存）。
if(NOT SR_BUILD_IO OR NOT SR_BUILD_RUNTIME)
    message(STATUS "SR_BUILD_IO/SR_BUILD_RUNTIME 关闭：只跑架构门禁，跳过单元测试与像素回归")
    return()
endif()

if(NOT TARGET GTest::gtest_main)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    add_subdirectory("${PROJECT_SOURCE_DIR}/third/googletest-main" "${PROJECT_BINARY_DIR}/third/googletest-main" EXCLUDE_FROM_ALL)
endif()

file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/golden" SR_GOLDEN_DIR_PATH)
# 单测按模块分组，一个文件只覆盖一个模块（见 docs/adr/ADR-004-module-boundaries.md）。
add_executable(unit_tests
    unit/math_test.cpp
    unit/core_buffer_test.cpp
    unit/pixel_format_test.cpp
    unit/resource_test.cpp
    unit/render_pipeline_test.cpp
    unit/postprocess_test.cpp
    unit/renderer_test.cpp
    unit/coverage_test.cpp
    unit/coverage_math_test.cpp
    unit/draw2d_test.cpp
    unit/scene_test.cpp
    unit/animation_test.cpp
    unit/ecs_test.cpp
    unit/arena_test.cpp
    unit/command_buffer_test.cpp
    unit/runtime_test.cpp
    unit/io_test.cpp
    unit/gltf_test.cpp
    unit/gltf_animation_test.cpp
    unit/pixel_regression_support_test.cpp)
target_link_libraries(unit_tests PRIVATE GTest::gtest_main softrenderer sr_dev_flags)
# support/：pixel_regression.h 在 tests/support/，benchmark.h 在 benchmarks/support/
# src/io/：内部单测需要对 glTF 转换做白盒验证，显式获得私有实现头
# （安装包不包含该路径，下游拿不到解析器类型）。
target_include_directories(unit_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/benchmarks"
    "${PROJECT_SOURCE_DIR}/src/io" "${PROJECT_SOURCE_DIR}/third/tinygltf" "${PROJECT_SOURCE_DIR}/third/nlohmann")
target_compile_definitions(unit_tests PRIVATE "SR_GOLDEN_DIR=\"${SR_GOLDEN_DIR_PATH}\"")
include(GoogleTest)
gtest_discover_tests(unit_tests DISCOVERY_MODE PRE_TEST PROPERTIES TIMEOUT 30)
add_test(NAME unit_tests_all COMMAND unit_tests)
set_tests_properties(unit_tests_all PROPERTIES LABELS "fast" TIMEOUT 30)

add_executable(render_regression integration/render_regression_test.cpp)
target_link_libraries(render_regression PRIVATE GTest::gtest_main softrenderer sr_dev_flags)
target_include_directories(render_regression PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(render_regression PRIVATE "SR_GOLDEN_DIR=\"${SR_GOLDEN_DIR_PATH}\"")
if(SR_UPDATE_GOLDEN)
    target_compile_definitions(render_regression PRIVATE SR_UPDATE_GOLDEN)
    message(WARNING "SR_UPDATE_GOLDEN=ON: missing golden images will be created")
endif()
add_test(NAME render_regression COMMAND render_regression)
set_tests_properties(render_regression PROPERTIES LABELS "regression" TIMEOUT 30)

# 下游验证单独标记：快速迭代可只运行 fast|regression，CI 默认会覆盖 packaging。
foreach(mode IN ITEMS subdirectory installed)
    add_test(NAME consumer_${mode} COMMAND "${CMAKE_COMMAND}"
        "-DSR_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "-DSR_BINARY_DIR=${PROJECT_BINARY_DIR}"
        "-DSR_CONFIG=$<CONFIG>"
        "-DSR_GENERATOR=${CMAKE_GENERATOR}"
        "-DSR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
        "-DSR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}"
        "-DSR_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
        "-DSR_INSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}"
        "-DSR_MODE=${mode}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/integration/run_consumer.cmake")
    set_tests_properties(consumer_${mode} PROPERTIES LABELS "packaging" TIMEOUT 240 RUN_SERIAL TRUE)
endforeach()

