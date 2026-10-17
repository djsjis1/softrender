cmake_minimum_required(VERSION 3.20)
project(SoftRenderer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SR_IS_TOP_LEVEL OFF)
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(SR_IS_TOP_LEVEL ON)
endif()
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build configuration" FORCE)
endif()

option(SR_BUILD_EXAMPLES "Build renderer examples" ${SR_IS_TOP_LEVEL})
option(SR_BUILD_PRACTICE "Build practice programs" ${SR_IS_TOP_LEVEL})
option(SR_BUILD_TESTS "Build unit and integration tests" ${SR_IS_TOP_LEVEL})
option(SR_BUILD_BENCHMARKS "Build reproducible renderer benchmarks" OFF)
option(SR_ENABLE_SPDLOG "Enable spdlog logging for examples and practice" ON)
option(SR_ENABLE_SANITIZERS "Enable sanitizers only for Debug" OFF)
option(SR_UPDATE_GOLDEN "Create missing golden images (local maintenance only)" OFF)
include(GNUInstallDirs)
include(cmake/version.cmake)

# Sanitizer 涉及 STL 容器注解，必须在第三方与本项目中一致启用。
# 目录级配置是有意例外：在 add_subdirectory 之前设置，且严格限定 Debug。
if(SR_ENABLE_SANITIZERS)
    if(MSVC)
        add_compile_options($<$<CONFIG:Debug>:/fsanitize=address>)
        string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
        add_link_options($<$<CONFIG:Debug>:/INCREMENTAL:NO>)
    else()
        add_compile_options($<$<CONFIG:Debug>:-fsanitize=address,undefined>
                            $<$<CONFIG:Debug>:-fno-omit-frame-pointer>)
        add_link_options($<$<CONFIG:Debug>:-fsanitize=address,undefined>)
    endif()
endif()

# 固定每个单头库的实现单元，避免重复定义和未解析符号。
add_library(third_party_impl STATIC
    third/tinygltf/tiny_gltf_impl.cpp # stb_image + stb_image_write
    third/tiny_obj_loader_impl.cpp
    src/scene/gltf_loader_impl.cpp   # tinygltf
)
set_target_properties(third_party_impl PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(third_party_impl PUBLIC cxx_std_23)
target_include_directories(third_party_impl PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/third/stb>
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/third/tinygltf>
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/third/nlohmann>
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/third>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/softrenderer/stb>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/softrenderer/tinygltf>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/softrenderer/nlohmann>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/softrenderer>
)
target_compile_definitions(third_party_impl PUBLIC SR_HAS_STB_IMAGE)
if(MSVC)
    target_compile_options(third_party_impl PRIVATE /utf-8 /wd4819)
endif()

# 自有目标避免与父工程的 entt 目标冲突；安装目标名保持兼容。
add_library(sr_entt INTERFACE)
set_target_properties(sr_entt PROPERTIES EXPORT_NAME entt)
target_include_directories(sr_entt INTERFACE
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/third/entt/src>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/softrenderer>
)
target_compile_features(sr_entt INTERFACE cxx_std_23)

find_package(Threads REQUIRED)
add_library(softrenderer INTERFACE)
add_library(SoftRenderer::softrenderer ALIAS softrenderer)
target_include_directories(softrenderer INTERFACE
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/softrenderer>
)
target_compile_features(softrenderer INTERFACE cxx_std_23)
target_link_libraries(softrenderer INTERFACE third_party_impl sr_entt Threads::Threads)
if(MSVC)
    # 中文公共头和头内 fopen 的编译要求，随接口传播。
    target_compile_options(softrenderer INTERFACE /utf-8)
    target_compile_definitions(softrenderer INTERFACE _CRT_SECURE_NO_WARNINGS)
endif()

# 警告策略只约束本仓程序，不强加给下游。
add_library(sr_dev_flags INTERFACE)
if(MSVC)
    target_compile_options(sr_dev_flags INTERFACE /W4 /WX)
else()
    target_compile_options(sr_dev_flags INTERFACE -Wall -Wextra -Werror)
endif()
if(WIN32)
    target_compile_definitions(sr_dev_flags INTERFACE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

if(SR_ENABLE_SPDLOG AND (SR_BUILD_EXAMPLES OR SR_BUILD_PRACTICE))
    add_subdirectory(third/log EXCLUDE_FROM_ALL)
endif()
function(sr_enable_logging target)
    if(SR_ENABLE_SPDLOG AND TARGET mylog)
        target_compile_definitions(${target} PRIVATE SR_USE_SPDLOG)
        target_link_libraries(${target} PRIVATE mylog)
    endif()
endfunction()

# 仅影响本项目子目录，不改变父工程的程序输出位置。
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
if(SR_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
if(SR_BUILD_PRACTICE)
    add_subdirectory(practice)
endif()
if(SR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(SR_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
include(cmake/install.cmake)
