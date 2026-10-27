# ============================================================
# 组件目标定义
#
# 分层与依赖方向（与 docs/adr/ADR-004-module-boundaries.md 一致）：
#   core    → 仅标准库（math / core / resource 的公共头）
#   render  → core（不依赖 scene / io / runtime / platform）
#   scene   → core（不依赖加载器或窗口）
#   runtime → core + 宿主线程能力
#   io      → scene / core + 私有第三方解析器（STATIC，解析器头不外露）
#
# 所有组件共用同一个公共 include 根目录 include/softrenderer；
# 分层由头文件依赖白名单测试强制，而不是靠 include 路径隔离。
# ============================================================

include(GNUInstallDirs)

set(SR_PUBLIC_INCLUDE_DIRS
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# 第三方实现单元：只提供符号，不导出任何第三方头。
# 只在构建 io 组件时才存在：关闭 io 的纯核心库不编译任何加载器。
if(SR_BUILD_IO)
add_library(third_party_impl STATIC
    src/io/stb_impl.cpp
    src/io/tinygltf_impl.cpp
    src/io/tinyobj_impl.cpp
)
set_target_properties(third_party_impl PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(third_party_impl PUBLIC cxx_std_23)
target_include_directories(third_party_impl PRIVATE
    ${PROJECT_SOURCE_DIR}/third/stb
    ${PROJECT_SOURCE_DIR}/third/tinygltf
    ${PROJECT_SOURCE_DIR}/third/nlohmann
    ${PROJECT_SOURCE_DIR}/third
)
if(MSVC)
    target_compile_options(third_party_impl PRIVATE /utf-8 /wd4819)
endif()
endif() # SR_BUILD_IO

# ---------------- core ----------------
add_library(softrenderer_core INTERFACE)
add_library(SoftRenderer::core ALIAS softrenderer_core)
set_target_properties(softrenderer_core PROPERTIES EXPORT_NAME core)
target_include_directories(softrenderer_core INTERFACE ${SR_PUBLIC_INCLUDE_DIRS})
target_compile_features(softrenderer_core INTERFACE cxx_std_23)
if(MSVC)
    target_compile_options(softrenderer_core INTERFACE /utf-8)
endif()

# ---------------- 裸机开关 ----------------
# core 是全部组件的 flag 源头：render/scene 链接 core，io/runtime 的自有源
# 文件也随之启用。SR_NO_EXCEPTIONS=ON 时消费方以 -fno-exceptions -fno-rtti
# 编译；公共头不含 throw 与 RTTI 依赖（由 tests 的 freestanding 头编译门禁
# 在 GCC/Clang 上强制）。MSVC 的 STL 组合不支持关闭异常，只禁 RTTI。
if(SR_NO_EXCEPTIONS)
    if(MSVC)
        target_compile_options(softrenderer_core INTERFACE /GR-)
    else()
        target_compile_options(softrenderer_core INTERFACE -fno-exceptions -fno-rtti)
    endif()
endif()

# ---------------- 裸机头文件门禁 ----------------
# 裸机承诺只覆盖 core/math/render/resource/scene（io/runtime 依赖异常与
# 线程，属宿主组件）。把这一子集的公共头逐个以 -fno-exceptions -fno-rtti
# 编译一遍：任何公共头新增 throw/RTTI/线程依赖都会在构建期失败。
# MSVC 的 STL 不支持关闭异常，只在 GCC/Clang 上启用（CI Linux 作业执行）。
# SR_NO_EXCEPTIONS=ON 时 tests 被关闭，所以门禁放在 targets.cmake 里，
# 裸机消费构建同样会执行它。
if(SR_IS_TOP_LEVEL AND NOT MSVC AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    file(GLOB_RECURSE SR_FREESTANDING_HEADERS RELATIVE "${PROJECT_SOURCE_DIR}/include"
        "${PROJECT_SOURCE_DIR}/include/softrenderer/core/*.h"
        "${PROJECT_SOURCE_DIR}/include/softrenderer/math/*.h"
        "${PROJECT_SOURCE_DIR}/include/softrenderer/render/*.h"
        "${PROJECT_SOURCE_DIR}/include/softrenderer/resource/*.h"
        "${PROJECT_SOURCE_DIR}/include/softrenderer/scene/*.h")
    # throw 文本级扫描：-fno-exceptions 对「未被实例化的类模板成员函数」不会
    # 报错（core/msaa_framebuffer.h 的尺寸校验函数曾因此穿过逐头编译门禁），
    # 所以对同一子集再做一次源码扫描。
    #
    # 剔除顺序必须是 字符串 → 行注释 → 块注释：
    #   不剔字符串，static_assert(x, "... throw ...") 这类断言文案会误报；
    #   反过来先剔注释，字符串里的 //（如 URL）会被当成注释截掉，同一行
    #   后面的真 throw 就漏了。字符串转义序列不展开，对公共头足够。
    foreach(header IN LISTS SR_FREESTANDING_HEADERS)
        file(READ "${PROJECT_SOURCE_DIR}/include/${header}" sr_header_content)
        string(REGEX REPLACE "\"([^\"\\\\]|\\\\.)*\"" "" sr_header_code "${sr_header_content}")
        string(REGEX REPLACE "//[^\n]*" "" sr_header_code "${sr_header_code}")
        string(REGEX REPLACE "/\\*([^*]|\\*[^/])*\\*/" "" sr_header_code "${sr_header_code}")
        # 补一个前导换行：前缀类 [^A-Za-z_0-9] 需要 throw 前面有字符，
        # 否则位于字符串首位的 throw 会漏判。
        set(sr_header_code "\n${sr_header_code}")
        if(sr_header_code MATCHES "[^A-Za-z_0-9]throw[^A-Za-z_0-9]")
            message(FATAL_ERROR
                "裸机子集的公共头不得出现 throw：include/${header}"
                "（该头以 -fno-exceptions 编译，throw 在无异常环境下无定义行为）")
        endif()
    endforeach()

    set(SR_FREESTANDING_TUS "")
    foreach(header IN LISTS SR_FREESTANDING_HEADERS)
        string(MAKE_C_IDENTIFIER "${header}" header_id)
        set(tu "${PROJECT_BINARY_DIR}/generated/freestanding_header_${header_id}.cpp")
        file(WRITE "${tu}" "#include <${header}>\nvoid sr_freestanding_${header_id}() {}\n")
        list(APPEND SR_FREESTANDING_TUS "${tu}")
    endforeach()
    add_library(sr_freestanding_headers_check OBJECT ${SR_FREESTANDING_TUS})
    target_link_libraries(sr_freestanding_headers_check PRIVATE SoftRenderer::core sr_dev_flags)
    target_compile_options(sr_freestanding_headers_check PRIVATE -fno-exceptions -fno-rtti)
endif()

# ---------------- render ----------------
add_library(softrenderer_render INTERFACE)
add_library(SoftRenderer::render ALIAS softrenderer_render)
set_target_properties(softrenderer_render PROPERTIES EXPORT_NAME render)
target_link_libraries(softrenderer_render INTERFACE softrenderer_core)

# ---------------- scene ----------------
add_library(softrenderer_scene INTERFACE)
add_library(SoftRenderer::scene ALIAS softrenderer_scene)
set_target_properties(softrenderer_scene PROPERTIES EXPORT_NAME scene)
target_link_libraries(softrenderer_scene INTERFACE softrenderer_core)

# ---------------- runtime ----------------
set(SR_ENABLED_COMPONENTS core render scene)
if(SR_BUILD_RUNTIME)
    add_library(softrenderer_runtime INTERFACE)
    add_library(SoftRenderer::runtime ALIAS softrenderer_runtime)
    set_target_properties(softrenderer_runtime PROPERTIES EXPORT_NAME runtime)
    target_link_libraries(softrenderer_runtime INTERFACE softrenderer_core Threads::Threads)
    list(APPEND SR_ENABLED_COMPONENTS runtime)
endif()

# ---------------- io ----------------
if(SR_BUILD_IO)
    add_library(softrenderer_io STATIC
        src/io/gltf_loader.cpp
        src/io/image_loader.cpp
        src/io/image_write.cpp
        src/io/obj_loader.cpp
        src/io/tga.cpp
    )
    add_library(SoftRenderer::io ALIAS softrenderer_io)
    set_target_properties(softrenderer_io PROPERTIES EXPORT_NAME io)
    target_link_libraries(softrenderer_io PUBLIC softrenderer_scene PRIVATE third_party_impl)
    target_include_directories(softrenderer_io PRIVATE
        ${PROJECT_SOURCE_DIR}/src/io
        ${PROJECT_SOURCE_DIR}/third/stb
        ${PROJECT_SOURCE_DIR}/third/tinygltf
        ${PROJECT_SOURCE_DIR}/third/nlohmann
        ${PROJECT_SOURCE_DIR}/third
    )
    if(MSVC)
        target_compile_options(softrenderer_io PRIVATE /utf-8 /W4 /WX)
        target_compile_definitions(softrenderer_io PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(softrenderer_io PRIVATE -Wall -Wextra -Werror)
    endif()
    list(APPEND SR_ENABLED_COMPONENTS io)
endif()

# ---------------- entt（可选适配）----------------
if(SR_ENABLE_ENTT)
    # 自有目标避免与父工程的 entt 目标冲突；导出名保持 entt。
    add_library(sr_entt INTERFACE)
    add_library(SoftRenderer::entt ALIAS sr_entt)
    set_target_properties(sr_entt PROPERTIES EXPORT_NAME entt)
    # SYSTEM：entt 以公共头形式进入使用者编译单元，第三方告警不应被
    # 本仓的 /W4 /WX（或 -Werror）升级成错误。
    target_include_directories(sr_entt SYSTEM INTERFACE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/third/entt/src>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/softrenderer>
    )
    target_compile_features(sr_entt INTERFACE cxx_std_23)
    list(APPEND SR_ENABLED_COMPONENTS entt)
endif()

# ---------------- 聚合目标 ----------------
add_library(softrenderer INTERFACE)
add_library(SoftRenderer::softrenderer ALIAS softrenderer)
target_link_libraries(softrenderer INTERFACE softrenderer_core)
foreach(component IN LISTS SR_ENABLED_COMPONENTS)
    if(NOT component STREQUAL "core")
        target_link_libraries(softrenderer INTERFACE SoftRenderer::${component})
    endif()
endforeach()

# 安装时导出实际构建出的组件清单，供 Config 文件判断默认导入范围。
set(SR_EXPORTED_COMPONENTS "${SR_ENABLED_COMPONENTS}")
