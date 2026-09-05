# ============================================================
# SoftRenderer 版本定义
# ============================================================
set(SOFTRENDERER_VERSION_MAJOR 1)
set(SOFTRENDERER_VERSION_MINOR 0)
set(SOFTRENDERER_VERSION_PATCH 0)
set(SOFTRENDERER_VERSION "${SOFTRENDERER_VERSION_MAJOR}.${SOFTRENDERER_VERSION_MINOR}.${SOFTRENDERER_VERSION_PATCH}")

# 生成版本头文件
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/softrenderer/version.h
    @ONLY
)
