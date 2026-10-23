# ============================================================
# SoftRenderer 安装规则
#
# 安装后其他项目可通过 find_package(SoftRenderer [COMPONENTS ...]) 使用：
#   find_package(SoftRenderer REQUIRED COMPONENTS core render)
#   target_link_libraries(myapp PRIVATE SoftRenderer::core SoftRenderer::render)
#
# 只安装明确的公共接口：
#   - include/softrenderer/ 下的公共头（不含 src/ 私有实现头）
#   - 各组件目标与第三方实现库
#   - 可选 EnTT 适配组件的公共头
# tests / benchmarks / 私有实现头 / 第三方解析器头一律不安装。
# ============================================================
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# 安装公共头：唯一正式入口 include/softrenderer/
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
)

# 安装生成的版本头（PROJECT_VERSION 是版本号的唯一来源）
install(DIRECTORY ${PROJECT_BINARY_DIR}/generated/softrenderer/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/softrenderer
    FILES_MATCHING PATTERN "*.h"
)

# EnTT 的公共头只随可选适配组件安装（保留许可证）
if(SR_ENABLE_ENTT)
    install(DIRECTORY ${PROJECT_SOURCE_DIR}/third/entt/src/entt
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/softrenderer
    )
    install(FILES ${PROJECT_SOURCE_DIR}/third/entt/LICENSE
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/softrenderer/entt
        OPTIONAL
    )
endif()

# 组件目标：按实际构建结果导出
set(SR_INSTALL_TARGETS
    softrenderer
    softrenderer_core
    softrenderer_render
    softrenderer_scene
)
if(SR_BUILD_IO)
    list(APPEND SR_INSTALL_TARGETS third_party_impl softrenderer_io)
endif()
if(SR_BUILD_RUNTIME)
    list(APPEND SR_INSTALL_TARGETS softrenderer_runtime)
endif()
if(SR_ENABLE_ENTT)
    list(APPEND SR_INSTALL_TARGETS sr_entt)
endif()

install(TARGETS ${SR_INSTALL_TARGETS} EXPORT SoftRendererTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(EXPORT SoftRendererTargets
    FILE SoftRendererTargets.cmake
    NAMESPACE SoftRenderer::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SoftRenderer
)

# 生成 Config 文件
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/SoftRendererConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/SoftRendererConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SoftRenderer
)

# 生成版本文件
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/SoftRendererConfigVersion.cmake
    VERSION ${SOFTRENDERER_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/SoftRendererConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/SoftRendererConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SoftRenderer
)
