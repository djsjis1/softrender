# C++23：vec.h 的 requires 约束子句 / std::same_as 等需要 C++20 及以上语法
#（CMAKE_CXX_STANDARD 23 需要 CMake 3.20+）
cmake_minimum_required(VERSION 3.20)
project(SoftRenderer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# 版本管理
include(cmake/version.cmake)

# 可选：spdlog 日志系统（third/log）
option(SR_ENABLE_SPDLOG "Enable spdlog logging via third/log" ON)
if(SR_ENABLE_SPDLOG)
    add_subdirectory(third/log EXCLUDE_FROM_ALL)
endif()

# stb_image + stb_image_write + tinygltf：统一实现库
# tinygltf 内部会 #define STB_IMAGE_IMPLEMENTATION 并 include stb_image.h
# 所以一个 .cpp 文件同时提供 stb + tinygltf 的所有符号
add_library(third_party_impl STATIC
    third/tinygltf/tiny_gltf_impl.cpp
    third/tiny_obj_loader_impl.cpp
    src/scene/gltf_loader_impl.cpp
)
target_include_directories(third_party_impl PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/third/stb>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/third/tinygltf>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/third/nlohmann>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/third>
    $<INSTALL_INTERFACE:include/softrenderer/stb>
    $<INSTALL_INTERFACE:include/softrenderer/tinygltf>
    $<INSTALL_INTERFACE:include/softrenderer/nlohmann>
    $<INSTALL_INTERFACE:include/softrenderer>
)
target_compile_definitions(third_party_impl PUBLIC SR_HAS_STB_IMAGE)
if(MSVC)
    # 第三方库文件可能含非当前代码页字符，显式按 UTF-8 解析并抑制 C4819
    target_compile_options(third_party_impl PRIVATE /utf-8 /wd4819)
endif()

# EnTT：ECS 框架（header-only）
add_library(entt INTERFACE)
target_include_directories(entt INTERFACE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/third/entt/src>
    $<INSTALL_INTERFACE:include/softrenderer/entt>
)
target_compile_features(entt INTERFACE cxx_std_23)

# 辅助函数：为示例启用 spdlog 日志
function(sr_enable_logging target)
    if(SR_ENABLE_SPDLOG AND TARGET mylog)
        target_compile_definitions(${target} PRIVATE SR_USE_SPDLOG)
        target_link_libraries(${target} PRIVATE mylog)
    endif()
endfunction()

# 开启常用警告，代码要求零警告通过
if(MSVC)
    # /utf-8：源码含中文注释，强制按 UTF-8 解析；/W4：高警告级别
    add_compile_options(/utf-8 /W4)
    # 允许使用可移植的标准 std::fopen 等，抑制 MSVC 专有的 C4996 弃用警告
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra)
endif()

# ============================================================
# 分层说明：
#   src/        = 渲染器本体，100% 平台无关，可作为纯头文件库复用。
#                 全局 include，任何目标都能 #include "core/..." 等。
#   platform/   = 使用层的显示/输入后端（平台相关，如 Win32）。
#                 只对需要开窗的 realtime 示例可见，从编译层面保证
#                 渲染器本体永远接触不到平台代码。
# ============================================================

# 渲染器本体的头文件根目录（平台无关）
include_directories(${CMAKE_SOURCE_DIR}/src)
include_directories(${CMAKE_BINARY_DIR}/generated) # 版本头
include_directories(${CMAKE_SOURCE_DIR}/third)     # tinyobjloader
include_directories(${CMAKE_SOURCE_DIR}/third/stb)  # stb_image / stb_image_write
include_directories(${CMAKE_SOURCE_DIR}/third/tinygltf) # tinygltf
include_directories(${CMAKE_SOURCE_DIR}/third/nlohmann) # nlohmann/json (tinygltf 依赖)

# Header-only INTERFACE 库目标（供外部 find_package 使用）
add_library(softrenderer INTERFACE)
target_include_directories(softrenderer INTERFACE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>
    $<INSTALL_INTERFACE:include/softrenderer>
)
target_compile_features(softrenderer INTERFACE cxx_std_23)
target_link_libraries(softrenderer INTERFACE third_party_impl entt)
add_library(SoftRenderer::softrenderer ALIAS softrenderer)

# 全局禁用 windows.h 的 min/max 宏，避免破坏 std::min/std::max
if(WIN32)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# 输出目录：可执行文件统一放到 build/bin
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 所有示例自动链接渲染器库（含第三方依赖）
link_libraries(softrenderer)

# 各阶段示例，逐阶段取消注释/追加
add_executable(stage1_lines examples/stage1_lines.cpp)
add_executable(stage2_triangles examples/stage2_triangles.cpp)
add_executable(stage3_cube examples/stage3_cube.cpp)
add_executable(stage4_zbuffer examples/stage4_zbuffer.cpp)
add_executable(stage5_texture examples/stage5_texture.cpp)

# 像素画示例：静态勇者（导出 PNG）+ 实时动画（窗口 + 帧序列）
add_executable(stage1_sprite examples/stage1_sprite.cpp)
add_executable(stage1_sprite_anim examples/stage1_sprite_anim.cpp)
target_include_directories(stage1_sprite_anim PRIVATE ${CMAKE_SOURCE_DIR})
if(WIN32)
    target_link_libraries(stage1_sprite_anim PRIVATE gdi32 user32)
endif()

# 阶段 6：实时窗口（Win32 GDI）。gdi32 用于 SetDIBitsToDevice，
# user32 用于窗口/消息（MSVC 通常自动链接，这里显式写出更稳妥）
add_executable(stage6_realtime examples/stage6_realtime.cpp)
target_include_directories(stage6_realtime PRIVATE ${CMAKE_SOURCE_DIR})
if(WIN32)
    target_link_libraries(stage6_realtime PRIVATE gdi32 user32)
endif()
sr_enable_logging(stage6_realtime)

# 阶段 6 的离屏验证：用同一套 Renderer+Shader 渲染一帧写 TGA
add_executable(stage6_offline examples/stage6_offline.cpp)

# 阶段 7：近平面裁剪对比
add_executable(stage7_clip examples/stage7_clip.cpp)
 
# 阶段 8：Shadow Mapping + 法线贴图
add_executable(stage8_offline examples/stage8_offline.cpp)
add_executable(stage8_realtime examples/stage8_realtime.cpp)
target_include_directories(stage8_realtime PRIVATE ${CMAKE_SOURCE_DIR})
if(WIN32)
    target_link_libraries(stage8_realtime PRIVATE gdi32 user32)
endif()
sr_enable_logging(stage8_realtime)

# 阶段 9：MSAA 抗锯齿
add_executable(stage9_msaa examples/stage9_msaa.cpp)

# 阶段 10：PBR 光照
add_executable(stage10_pbr examples/stage10_pbr.cpp)

# 阶段 11：后处理管线
add_executable(stage11_postprocess examples/stage11_postprocess.cpp)

# 阶段 12：多线程光栅化
find_package(Threads REQUIRED)
add_executable(stage12_threaded examples/stage12_threaded.cpp)
target_link_libraries(stage12_threaded PRIVATE Threads::Threads)

# 演示：群星闪耀
add_executable(demo_stars examples/demo_stars.cpp)

# 演示：glTF 加载 + Profiler
add_executable(demo_gltf examples/demo_gltf.cpp)
target_link_libraries(demo_gltf PRIVATE third_party_impl)

# 演示：glTF PBR 材质渲染
add_executable(demo_pbr_material examples/demo_pbr_material.cpp)
target_link_libraries(demo_pbr_material PRIVATE third_party_impl)

# 演示：IBL + PBR 完整场景
add_executable(demo_ibl_pbr examples/demo_ibl_pbr.cpp)

# 演示：骨骼蒙皮动画
add_executable(demo_skinning examples/demo_skinning.cpp)
target_link_libraries(demo_skinning PRIVATE third_party_impl)

# 演示：2D 渲染子系统（纯 2D 场景 + 2D HUD 叠加 3D）
add_executable(demo_2d examples/demo_2d.cpp)

# ============================================================
# 练习代码：practice/ 目录
#
# 约定：把使用本库的练习源码（每个 .cpp 一个独立程序）放进 practice/，
#       重新构建即自动生成可执行程序：
#         目标名 = practice_<文件名>，输出 build/bin/<Config>/practice_<文件名>.exe
#
# sr_add_practice(<source>) 会自动完成：
#   - 链接渲染器库 softrenderer（含第三方依赖）
#   - 把 ${CMAKE_SOURCE_DIR} 加入头文件搜索路径，可直接
#     #include "platform/win32_window.h" 写实时窗口练习
#   - Windows 上链接 gdi32/user32（窗口后端所需），并启用可选日志
#
# file(GLOB ... CONFIGURE_DEPENDS)：新增/删除练习文件后，构建会自动重新配置，
# 无需手动改 CMakeLists。
# ============================================================
function(sr_add_practice source_file)
    get_filename_component(_practice_name "${source_file}" NAME_WE)
    set(_practice_target "practice_${_practice_name}")
    add_executable(${_practice_target} "${source_file}")
    target_include_directories(${_practice_target} PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(${_practice_target} PRIVATE softrenderer)
    if(WIN32)
        target_link_libraries(${_practice_target} PRIVATE gdi32 user32)
    endif()
    sr_enable_logging(${_practice_target})
endfunction()

file(GLOB SR_PRACTICE_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/practice/*.cpp")
foreach(_practice_src ${SR_PRACTICE_SOURCES})
    sr_add_practice("${_practice_src}")
endforeach()

# 单元测试（Google Test，源码在 third/googletest-main）
enable_testing()
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
add_subdirectory(third/googletest-main EXCLUDE_FROM_ALL)
add_executable(unit_tests tests/test_core.cpp)
target_link_libraries(unit_tests PRIVATE GTest::gtest_main softrenderer)
include(GoogleTest)
gtest_discover_tests(unit_tests)

# 安装规则
include(cmake/install.cmake)
