add_executable(renderer_benchmark renderer_benchmark.cpp)
# 基准只用 render/core（绘制 + 程序化网格），不依赖 io / runtime 组件。
target_link_libraries(renderer_benchmark PRIVATE SoftRenderer::render sr_dev_flags)

# 压测/回归探针：覆盖渲染、后处理、2D、IO、线程、内存与敌意输入。
# 需要 io（模型/图像加载）与 runtime（线程池/缓存/Profiler），因此跟随聚合目标。
# 与 renderer_benchmark 分开：后者要能只链 render 组件，用于验证组件边界。
add_executable(renderer_stress stress_main.cpp)
target_link_libraries(renderer_stress PRIVATE SoftRenderer::softrenderer)
if(MSVC)
    target_compile_options(renderer_stress PRIVATE /utf-8)
endif()
