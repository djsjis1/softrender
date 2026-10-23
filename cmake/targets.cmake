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
    target_compile_definitions(softrenderer_core INTERFACE _CRT_SECURE_NO_WARNINGS)
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
    target_include_directories(sr_entt INTERFACE
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
