if(NOT TARGET GTest::gtest_main)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    add_subdirectory("${PROJECT_SOURCE_DIR}/third/googletest-main" "${PROJECT_BINARY_DIR}/third/googletest-main" EXCLUDE_FROM_ALL)
endif()

file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/golden" SR_GOLDEN_DIR_PATH)
add_executable(unit_tests unit/test_core.cpp unit/renderer_test.cpp unit/arena_test.cpp unit/coverage_test.cpp
    unit/gltf_test.cpp unit/animation_test.cpp unit/command_buffer_test.cpp unit/coverage_math_test.cpp)
target_link_libraries(unit_tests PRIVATE GTest::gtest_main softrenderer sr_dev_flags)
target_compile_definitions(unit_tests PRIVATE "SR_GOLDEN_DIR=\"${SR_GOLDEN_DIR_PATH}\"")
include(GoogleTest)
gtest_discover_tests(unit_tests DISCOVERY_MODE PRE_TEST PROPERTIES TIMEOUT 30)
add_test(NAME unit_tests_all COMMAND unit_tests)
set_tests_properties(unit_tests_all PROPERTIES LABELS "fast" TIMEOUT 30)

add_executable(render_regression integration/render_regression_test.cpp)
target_link_libraries(render_regression PRIVATE GTest::gtest_main softrenderer sr_dev_flags)
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

