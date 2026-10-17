# ============================================================
# SoftRenderer 版本定义
# ============================================================
set(SOFTRENDERER_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(SOFTRENDERER_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(SOFTRENDERER_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(SOFTRENDERER_VERSION "${PROJECT_VERSION}")

# 生成版本头文件
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/softrenderer/version.h
    @ONLY
)
