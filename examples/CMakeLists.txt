# 示例需要 io 组件（写 TGA/PNG、加载模型）与 runtime 组件（Profiler/线程）。
if(NOT SR_BUILD_IO OR NOT SR_BUILD_RUNTIME)
    message(STATUS "SR_BUILD_IO/SR_BUILD_RUNTIME 关闭：跳过示例（它们依赖加载与计时组件）")
    return()
endif()

# 每个示例都是独立程序，保留原有 target 名称。
# stage 编号即学习顺序：01-14 为线性主线，demo_* 为综合演示。
function(sr_add_example name)
    add_executable(${name} "${name}.cpp")
    target_link_libraries(${name} PRIVATE softrenderer sr_dev_flags)
endfunction()

# ── 线性主线（14 个阶段）──
foreach(name IN ITEMS
    stage01_framebuffer stage02_sprite stage04_triangle stage05_transform
    stage06_depth stage07_texture stage08_shader stage09_clip
    stage10_shadow stage11_msaa stage12_pbr stage13_postprocess
    stage14_threaded demo_stars demo_gltf demo_pbr_material
    demo_ibl_pbr demo_skinning demo_2d demo_forward)
    sr_add_example(${name})
endforeach()

# 必须守卫整个目标；非 Windows 下窗口类并不存在。
if(WIN32)
    foreach(name IN ITEMS stage03_sprite_anim stage08_shader_live stage10_shadow_live)
        sr_add_example(${name})
        target_include_directories(${name} PRIVATE "${PROJECT_SOURCE_DIR}")
        target_link_libraries(${name} PRIVATE gdi32 user32)
    endforeach()
    sr_enable_logging(stage08_shader_live)
    sr_enable_logging(stage10_shadow_live)
endif()
