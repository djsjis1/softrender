# 练习使用完整的渲染/加载/计时能力；缺失组件时跳过并报告原因。
if(NOT SR_BUILD_IO OR NOT SR_BUILD_RUNTIME)
    message(STATUS "SR_BUILD_IO/SR_BUILD_RUNTIME 关闭：跳过 practice")
    return()
endif()

# 练习允许自动发现；库源码与测试使用显式文件清单。
file(GLOB SR_PRACTICE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
foreach(source IN LISTS SR_PRACTICE_SOURCES)
    get_filename_component(name "${source}" NAME_WE)
    set(target "practice_${name}")
    add_executable(${target} "${source}")
    target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}")
    target_link_libraries(${target} PRIVATE softrenderer sr_dev_flags)
    if(WIN32)
        target_link_libraries(${target} PRIVATE gdi32 user32)
    endif()
    sr_enable_logging(${target})
endforeach()
