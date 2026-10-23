add_executable(renderer_benchmark renderer_benchmark.cpp)
# 基准只用 render/core（绘制 + 程序化网格），不依赖 io / runtime 组件。
target_link_libraries(renderer_benchmark PRIVATE SoftRenderer::render sr_dev_flags)
