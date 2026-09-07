# C++23：vec.h 的 requires 约束子句 / std::same_as 等需要 C++20 及以上语法
#（CMAKE_CXX_STANDARD 23 需要 CMake 3.20+）
cmake_minimum_required(VERSION 3.20)
project(SoftRenderer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ============================================================
# Sanitizer（必须放在所有 add_subdirectory 之前，理由见下）
#
# cpp_project_rules.md「构建配置」要求：Debug 必须开启 ASAN/UBSAN，
# RelWithDebInfo 不开启，Release **禁止**开启。
#
# 为何重要：TASK-003 修的整数溢出 UB 与 Release 越界读，正是 ASAN/UBSAN
# 能自动抓到的缺陷类型。本项目此前没有任何机制能发现这类问题，
# 这解释了它们为何能长期存在。
# ============================================================
option(SR_ENABLE_SANITIZERS "仅在 Debug 配置下开启 Sanitizer（Release 永不开启）" OFF)

if(SR_ENABLE_SANITIZERS)
    # 【为何用全局 add_compile_options 而不放在 sr_dev_flags 里】
    # Sanitizer 是 **ABI 级**设置，不是逐目标的开发偏好：同一次链接里的所有
    # 目标必须一致。实测证据：把 /fsanitize=address 只加到项目目标上时，
    # googletest（由 add_subdirectory 引入，不链接 sr_dev_flags）未开 ASAN，
    # 链接 unit_tests 时报 LNK2038："annotate_string" / "annotate_vector" 不匹配
    # （MSVC ASAN 会给 STL 容器加注解，两侧必须同开或同关）+ LNK1319。
    #
    # 这是 engineering_rules.md「禁止全局指令」的一个**有意例外**：例外理由已在此
    # 说明，且仅在 SR_ENABLE_SANITIZERS=ON（默认 OFF）时激活，不影响正常构建，
    # 也不会传播给下游集成方（对方自己配置时本选项为 OFF）。
    #
    # 【为何必须在 add_subdirectory 之前】add_compile_options 改的是目录属性，
    # 只对**其后**创建的目标与添加的子目录生效。third/log（spdlog）在第 19 行
    # 就 add_subdirectory 了，若本块放在后面，mylog/spdlog 不会开 ASAN，
    # 而 realtime 示例链接 mylog → 同样的 LNK2038。
    #
    # 【为何用 $<$<CONFIG:Debug>:...> 而非 if(CMAKE_BUILD_TYPE STREQUAL "Debug")】
    # MSVC 是**多配置**生成器，CMAKE_BUILD_TYPE 为空，一次配置会产出 Debug 与
    # Release 两套产物。只有生成器表达式能把标志精确限定到 Debug 配置，
    # 从而真正满足「Release 禁止开启 Sanitizer」，而不是靠人工记得不要混用。
    # 单配置生成器（Makefile / Ninja）下 $<CONFIG:Debug> 同样按 CMAKE_BUILD_TYPE 求值。
    if(MSVC)
        # MSVC 的 /fsanitize=address 只支持 ASAN，**不支持 UBSAN**；
        # UBSAN 检查交给 CI 的 ubuntu / macos job（见 docs/troubleshooting.md）。
        add_compile_options($<$<CONFIG:Debug>:/fsanitize=address>)

        # /RTC1（基本运行时检查）与 ASAN **互斥**，而 MSVC 的 Debug 配置默认带它，
        # 不移除会直接编译失败。
        string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")

        # ASAN 与增量链接不兼容；Debug 配置默认 /INCREMENTAL，同样需去掉。
        string(REPLACE "/INCREMENTAL" "/INCREMENTAL:NO" CMAKE_EXE_LINKER_FLAGS_DEBUG
               "${CMAKE_EXE_LINKER_FLAGS_DEBUG}")

        message(STATUS "SR_ENABLE_SANITIZERS=ON：Debug 配置启用 MSVC AddressSanitizer"
                       "（已移除 /RTC1 与增量链接；MSVC 不支持 UBSAN）")
    else()
        # GCC / Clang：ASAN + UBSAN 同时开。-fno-omit-frame-pointer 让栈回溯可读。
        add_compile_options(
            $<$<CONFIG:Debug>:-fsanitize=address,undefined>
            $<$<CONFIG:Debug>:-fno-omit-frame-pointer>
        )
        # 链接阶段也必须传同样的 -fsanitize，否则报未定义的 asan 符号
        add_link_options($<$<CONFIG:Debug>:-fsanitize=address,undefined>)
        message(STATUS "SR_ENABLE_SANITIZERS=ON：Debug 配置启用 ASAN + UBSAN")
    endif()
endif()

# 版本管理
include(cmake/version.cmake)

# 可选：spdlog 日志系统（third/log）
option(SR_ENABLE_SPDLOG "Enable spdlog logging via third/log" ON)
if(SR_ENABLE_SPDLOG)
    add_subdirectory(third/log EXCLUDE_FROM_ALL)
endif()

# 第三方 header-only 库的统一实现库。
#
# 为什么需要它：stb / tinygltf / tinyobjloader 都靠「在**恰好一个** .cpp 里
# #define XXX_IMPLEMENTATION」来产出符号。若多个编译单元都定义，会符号重定义；
# 若一个都不定义，链接时报未解析符号。故每个库固定一个实现单元。
#
# 三个实现单元的真实分工（⚠ 与文件名给人的直觉不同，改动前请先读源文件）：
#   third/tinygltf/tiny_gltf_impl.cpp → stb_image + stb_image_write（**不含 tinygltf**）
#   src/scene/gltf_loader_impl.cpp    → tinygltf
#   third/tiny_obj_loader_impl.cpp    → tinyobjloader
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

# ============================================================
# 分层说明：
#   src/        = 渲染器本体，100% 平台无关，可作为纯头文件库复用。
#                 任何目标链接 softrenderer 后即可 #include "core/..." 等。
#   platform/   = 使用层的显示/输入后端（平台相关，如 Win32）。
#                 只对需要开窗的 realtime 示例可见，从编译层面保证
#                 渲染器本体永远接触不到平台代码。
# ============================================================

# Header-only INTERFACE 库目标（供外部 find_package 使用）
#
# 本目标携带的是**公共接口要求**：下游集成方编译本项目头文件时真正必需的东西。
# 与之相对，项目自己的开发偏好（警告级别、Win32 宏）放在下面的 sr_dev_flags，
# **不向下游传播** —— 把本项目的警告策略强加给集成方是不合理的。
add_library(softrenderer INTERFACE)
target_include_directories(softrenderer INTERFACE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>
    $<INSTALL_INTERFACE:include/softrenderer>
)
target_compile_features(softrenderer INTERFACE cxx_std_23)
target_link_libraries(softrenderer INTERFACE third_party_impl entt)

# /utf-8 属接口要求而非开发偏好：src/ 头文件含大量中文注释，
# MSVC 不指定编码时会按本地代码页解析并报 C4819，下游同样会遇到。
# _CRT_SECURE_NO_WARNINGS 也是接口要求：core/tga.h 在头文件内直接调 std::fopen，
# 该调用在使用方的编译单元里展开，MSVC 会在使用方报 C4996。
# （根治办法是把 tga.h 的 I/O 改成不依赖弃用函数，但超出本次改造范围。）
if(MSVC)
    target_compile_options(softrenderer INTERFACE /utf-8)
    target_compile_definitions(softrenderer INTERFACE _CRT_SECURE_NO_WARNINGS)
endif()

add_library(SoftRenderer::softrenderer ALIAS softrenderer)

# 项目内部开发标志（INTERFACE 库，只给本仓的示例 / 练习 / 测试目标链接）
add_library(sr_dev_flags INTERFACE)
# 零警告原则（cpp_project_rules.md）：要求在 -Wall -Wextra -Werror 下无警告编译。
#
# 【为何现在就能开 /WX】实测多次全量构建在 /W4 下已是**零警告**，
# 所以开启 /WX 不会引入新的编译失败，只是把已有约定变成构建期强制。
#
# 【作用范围】sr_dev_flags 只被本仓的示例 / 练习 / 测试目标链接，因此：
#   - third_party_impl 不受影响（它只有自己的 PRIVATE /utf-8 /wd4819）
#   - googletest / spdlog 不受影响（由 add_subdirectory 引入，不链接本目标）
#   - 下游集成方不受影响（本目标不导出）
# 这一点很关键：若把 /W4 /WX 放进 softrenderer INTERFACE，就会把本项目的
# 警告策略强加给所有集成方，包括他们自己的代码。
if(MSVC)
    target_compile_options(sr_dev_flags INTERFACE /W4 /WX)
else()
    target_compile_options(sr_dev_flags INTERFACE -Wall -Wextra -Werror)
endif()
# 禁用 windows.h 的 min/max 宏，避免破坏 std::min/std::max。
# 只对引用 windows.h 的目标有意义（即 platform/win32_window.h 与三个 realtime 示例）；
# src/ 从不 include windows.h（已全仓扫描验证，并录入 engineering_rules.md 红线）。
if(WIN32)
    target_compile_definitions(sr_dev_flags INTERFACE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# 输出目录：可执行文件统一放到 build/bin
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 示例目标统一注册：显式链接渲染器库 + 开发标志。
#
# 为何不用 link_libraries(softrenderer) 全局设置：cpp_project_rules.md「构建系统规范」
# 明令禁止 include_directories() / link_libraries() 全局设置，要求统一用
# target_* 并显式声明 PRIVATE/PUBLIC 可见性。全局指令的问题在于它隐式作用于
# **所有**目标（包括将来可能新增的第三方子项目），且无法从目标定义看出依赖。
function(sr_add_example name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE softrenderer sr_dev_flags)
endfunction()

# 实时窗口示例：仅 Windows，整个目标（含编译）都必须在守卫内。
#
# 为什么不只是链接库要守卫：platform/win32_window.h 的**全部内容**包在
# #ifdef _WIN32 内，非 Windows 平台该头展开为空、Win32Window 类不存在，
# 而这些示例无条件使用它 → 编译必然失败。旧写法只守住了
# target_link_libraries(gdi32 user32)，没守住编译本身，导致 CI 的
# ubuntu / macos 两个 job 100% 编译失败（见 docs/tasks/TASK-004）。
# CI 的 Verify platform guards 步骤会把本守卫变成回归测试。
#
# 跨平台的验证由各阶段对应的 *_offline 目标承担（同一套 Renderer+Shader）。
#
# enable_log 参数：只有 stage6_realtime / stage8_realtime 实际使用 SR_LOG_*，
# stage1_sprite_anim 不用。保留这个差异而不是统一开启，是为了不改变现有行为。
function(sr_add_realtime_example name source enable_log)
    if(NOT WIN32)
        return()
    endif()
    add_executable(${name} ${source})
    # ${CMAKE_SOURCE_DIR} 进头文件搜索路径，才能 #include "platform/win32_window.h"
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR})
    # gdi32 用于 SetDIBitsToDevice，user32 用于窗口/消息
    # （MSVC 通常自动链接，显式写出更稳妥）
    target_link_libraries(${name} PRIVATE softrenderer sr_dev_flags gdi32 user32)
    if(enable_log)
        sr_enable_logging(${name})
    endif()
endfunction()

# 各阶段示例，逐阶段取消注释/追加
sr_add_example(stage1_lines examples/stage1_lines.cpp)
sr_add_example(stage2_triangles examples/stage2_triangles.cpp)
sr_add_example(stage3_cube examples/stage3_cube.cpp)
sr_add_example(stage4_zbuffer examples/stage4_zbuffer.cpp)
sr_add_example(stage5_texture examples/stage5_texture.cpp)

# 像素画示例：静态勇者（导出 PNG，跨平台）
sr_add_example(stage1_sprite examples/stage1_sprite.cpp)
# 实时动画（窗口 + 帧序列）：仅 Windows，不用日志
sr_add_realtime_example(stage1_sprite_anim examples/stage1_sprite_anim.cpp OFF)

# 阶段 6：离屏验证（跨平台）+ 实时窗口（仅 Windows）
sr_add_example(stage6_offline examples/stage6_offline.cpp)
sr_add_realtime_example(stage6_realtime examples/stage6_realtime.cpp ON)

# 阶段 7：近平面裁剪对比
sr_add_example(stage7_clip examples/stage7_clip.cpp)

# 阶段 8：Shadow Mapping + 法线贴图
sr_add_example(stage8_offline examples/stage8_offline.cpp)
sr_add_realtime_example(stage8_realtime examples/stage8_realtime.cpp ON)

# 阶段 9：MSAA 抗锯齿
sr_add_example(stage9_msaa examples/stage9_msaa.cpp)

# 阶段 10：PBR 光照
sr_add_example(stage10_pbr examples/stage10_pbr.cpp)

# 阶段 11：后处理管线
sr_add_example(stage11_postprocess examples/stage11_postprocess.cpp)

# 阶段 12：多线程光栅化
find_package(Threads REQUIRED)
sr_add_example(stage12_threaded examples/stage12_threaded.cpp)
target_link_libraries(stage12_threaded PRIVATE Threads::Threads)

# 演示：群星闪耀
sr_add_example(demo_stars examples/demo_stars.cpp)

# 演示：glTF 加载 + Profiler
sr_add_example(demo_gltf examples/demo_gltf.cpp)

# 演示：glTF PBR 材质渲染
sr_add_example(demo_pbr_material examples/demo_pbr_material.cpp)

# 演示：IBL + PBR 完整场景
sr_add_example(demo_ibl_pbr examples/demo_ibl_pbr.cpp)

# 演示：骨骼蒙皮动画
sr_add_example(demo_skinning examples/demo_skinning.cpp)
# 注：旧版本对 demo_gltf / demo_pbr_material / demo_skinning 额外写了
# target_link_libraries(... PRIVATE third_party_impl)，已删除 ——
# softrenderer 已以 INTERFACE 链接 third_party_impl，传递可见，重复声明无作用。

# 演示：2D 渲染子系统（纯 2D 场景 + 2D HUD 叠加 3D）
sr_add_example(demo_2d examples/demo_2d.cpp)

# ============================================================
# 练习代码：practice/ 目录
#
# 约定：把使用本库的练习源码（每个 .cpp 一个独立程序）放进 practice/，
#       重新构建即自动生成可执行程序：
#         目标名 = practice_<文件名>，输出 build/bin/<Config>/practice_<文件名>.exe
#
# sr_add_practice(<source>) 会自动完成：
#   - 链接渲染器库 softrenderer（含第三方依赖）与开发标志 sr_dev_flags
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
    target_link_libraries(${_practice_target} PRIVATE softrenderer sr_dev_flags)
    if(WIN32)
        target_link_libraries(${_practice_target} PRIVATE gdi32 user32)
    endif()
    sr_enable_logging(${_practice_target})
endfunction()

file(GLOB SR_PRACTICE_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/practice/*.cpp")
foreach(_practice_src ${SR_PRACTICE_SOURCES})
    sr_add_practice("${_practice_src}")
endforeach()

# 基准图目录的绝对路径，unit_tests 与 render_regression 共用。
# 必须以绝对路径注入：ctest 的工作目录是 build/，而 pixel_regression.h 旧版
# 硬编码相对路径 "tests/golden/" 会解析成 build/tests/golden/ → 找不到基准图 →
# 旧逻辑自动创建并返回 passed → **测试永远假通过**。
# file(TO_CMAKE_PATH) 把 Windows 反斜杠转为正斜杠，避免路径中的 \ 在 C
# 字符串字面量里被当转义符。
file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}/tests/golden" SR_GOLDEN_DIR_PATH)

# 单元测试（Google Test，源码在 third/googletest-main）
enable_testing()
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
add_subdirectory(third/googletest-main EXCLUDE_FROM_ALL)
add_executable(unit_tests tests/test_core.cpp)
target_link_libraries(unit_tests PRIVATE GTest::gtest_main softrenderer sr_dev_flags)
# unit_tests 里的 PixelRegression 套件会验证 goldenPath 确实是绝对路径，
# 所以本目标也需要该定义；缺失时那个用例会 FAIL 而不是静默退化。
target_compile_definitions(unit_tests PRIVATE "SR_GOLDEN_DIR=\"${SR_GOLDEN_DIR_PATH}\"")
include(GoogleTest)
gtest_discover_tests(unit_tests)

# 开发迭代用的快速冒烟项：单进程跑完全部用例（实测 ~18 毫秒）。
#
# 为何需要它：gtest_discover_tests 把**每个用例**注册为一个独立 ctest 项，
# 120 用例 = 120 次进程创建，串行实测约 40 秒 —— 开销全在进程启动而非测试本身。
# 保留 discover 是为了 CI 失败时能精确定位到单个用例；开发时用
#   ctest --test-dir build -L fast
# 只跑这一项。CI 则用 ctest --parallel -LE fast（排除本项，避免与 discover 项重复跑）。
add_test(NAME unit_tests_all COMMAND unit_tests)
set_tests_properties(unit_tests_all PROPERTIES LABELS "fast")

# ============================================================
# 集成测试：端到端像素回归
#
# 单元测试只能断言局部性质，无法锁定「整条管线跑完画面是否对的」。
# rasterizer_fast 的深度插值权重错位能长期潜伏，正是因为缺这张网
# （详见 docs/tasks/DONE/TASK-002-光栅化器缺陷修复.md）。
#
# 基准图在 tests/golden/*.tga，已纳入版本控制（.gitignore 不排除该目录）。
# ============================================================
option(SR_UPDATE_GOLDEN "像素回归：基准图缺失时自动创建而非失败（仅本地更新基准时用，提交前必须关掉）" OFF)

add_executable(render_regression tests/integration/render_regression_test.cpp)
target_link_libraries(render_regression PRIVATE GTest::gtest_main softrenderer sr_dev_flags)
target_compile_definitions(render_regression PRIVATE "SR_GOLDEN_DIR=\"${SR_GOLDEN_DIR_PATH}\"")
if(SR_UPDATE_GOLDEN)
    target_compile_definitions(render_regression PRIVATE SR_UPDATE_GOLDEN)
    message(STATUS "SR_UPDATE_GOLDEN=ON：缺失的基准图将被自动创建。提交前请关掉本选项。")
endif()

# 不用 gtest_discover_tests：只有 3 个用例，整个二进制作为一项即可，
# 避开 3 次额外的进程启动。打 regression 标签，便于单独跑或排除：
#   ctest --test-dir build -L regression      # 只跑像素回归
#   ctest --test-dir build -LE regression     # 排除像素回归
add_test(NAME render_regression COMMAND render_regression)
set_tests_properties(render_regression PROPERTIES LABELS "regression")

# 安装规则
include(cmake/install.cmake)
