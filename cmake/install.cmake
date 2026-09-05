# ============================================================
# SoftRenderer 安装规则
#
# 安装后其他项目可通过 find_package(SoftRenderer) 使用：
#   find_package(SoftRenderer REQUIRED)
#   target_link_libraries(myapp PRIVATE SoftRenderer::softrenderer)
# ============================================================
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# 安装头文件（渲染器本体 = src/ 下所有 .h）
install(DIRECTORY ${CMAKE_SOURCE_DIR}/src/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/softrenderer
    FILES_MATCHING PATTERN "*.h"
)

# 安装生成的版本头
install(DIRECTORY ${CMAKE_BINARY_DIR}/generated/softrenderer/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/softrenderer
    FILES_MATCHING PATTERN "*.h"
)

# 安装 stb_image 头文件
install(FILES
    ${CMAKE_SOURCE_DIR}/third/stb/stb_image.h
    ${CMAKE_SOURCE_DIR}/third/stb/stb_image_write.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/softrenderer/stb
)

# 安装 tinyobjloader 头文件
install(FILES ${CMAKE_SOURCE_DIR}/third/tiny_obj_loader.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/softrenderer
)

# 安装 EnTT 头文件
install(DIRECTORY ${CMAKE_SOURCE_DIR}/third/entt/src/entt
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/softrenderer
)

# 导出目标（如果将来有 .lib/.a 的话；当前是 header-only，
# 但仍导出 INTERFACE 目标以便 find_package 设置 include 路径）
install(TARGETS softrenderer third_party_impl entt EXPORT SoftRendererTargets
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
