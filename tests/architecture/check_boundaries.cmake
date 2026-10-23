cmake_minimum_required(VERSION 3.20)

# ============================================================
# 架构边界检查（CTest 调用：cmake -DSR_INCLUDE_DIR=... -P check_boundaries.cmake）
#
# 规则（与 docs/adr/ADR-004-module-boundaries.md 对应）：
#   math     → 只允许标准库
#   core     → 允许 math/ 与标准库；禁止 io/、runtime/、platform/、第三方头
#   resource → 允许 core/、math/；禁止文件/日志/线程与第三方头
#   render   → 允许 resource/、core/、math/；禁止 scene/、io/、runtime/、platform/
#   scene    → 允许 resource/、core/、math/；禁止 io/、runtime/、platform/
#   runtime  → 允许 core/；禁止 io/、render/、scene/、platform/
#   io       → 允许 scene/、resource/、core/、math/；第三方头只允许出现在 src/ 实现里
# ============================================================

if(NOT DEFINED SR_INCLUDE_DIR)
    message(FATAL_ERROR "SR_INCLUDE_DIR is required")
endif()

set(FORBIDDEN_MATH "")
set(FORBIDDEN_CORE "io/;runtime/;platform/;stb_;tiny_gltf;tiny_obj;nlohmann;json.hpp;entt/")
set(FORBIDDEN_RESOURCE "io/;runtime/;platform/;stb_;tiny_gltf;tiny_obj;nlohmann;json.hpp;entt/")
set(FORBIDDEN_RENDER "scene/;io/;runtime/;platform/;stb_;tiny_gltf;tiny_obj;nlohmann;json.hpp;entt/")
set(FORBIDDEN_SCENE "io/;runtime/;platform/;stb_;tiny_gltf;tiny_obj;nlohmann;json.hpp;entt/")
set(FORBIDDEN_RUNTIME "io/;render/;scene/;platform/;stb_;tiny_gltf;tiny_obj;nlohmann;json.hpp")
set(FORBIDDEN_IO "platform/;stb_;tiny_gltf;tiny_obj;nlohmann;json.hpp")

file(GLOB_RECURSE HEADERS "${SR_INCLUDE_DIR}/softrenderer/*.h")
set(FAILURES "")

foreach(header IN LISTS HEADERS)
    file(RELATIVE_PATH rel "${SR_INCLUDE_DIR}/softrenderer" "${header}")
    string(REPLACE "\\" "/" rel "${rel}")

    set(module "")
    if(rel MATCHES "^math/")
        set(module math)
    elseif(rel MATCHES "^core/")
        set(module core)
    elseif(rel MATCHES "^resource/")
        set(module resource)
    elseif(rel MATCHES "^render/")
        set(module render)
    elseif(rel MATCHES "^scene/")
        set(module scene)
    elseif(rel MATCHES "^runtime/")
        set(module runtime)
    elseif(rel MATCHES "^io/")
        set(module io)
    else()
        continue() # 顶层头（version.h / namespace.h）不参与模块规则
    endif()

    string(TOUPPER "${module}" module_upper)
    set(forbidden ${FORBIDDEN_${module_upper}})

    file(STRINGS "${header}" include_lines REGEX "^[ \t]*#[ \t]*include")
    foreach(line IN LISTS include_lines)
        foreach(pattern IN LISTS forbidden)
            if(line MATCHES "${pattern}")
                list(APPEND FAILURES "${module}/${rel}: ${line}")
            endif()
        endforeach()
    endforeach()
endforeach()

# 模块内部不允许相对 include 越界到私有实现目录
file(GLOB_RECURSE PUBLIC_HEADERS "${SR_INCLUDE_DIR}/softrenderer/*.h")
foreach(header IN LISTS PUBLIC_HEADERS)
    file(STRINGS "${header}" bad_lines REGEX "src/io/|src/test/|benchmarks/|tests/")
    foreach(line IN LISTS bad_lines)
        if(line MATCHES "^[ \t]*#[ \t]*include")
            list(APPEND FAILURES "${header}: ${line}")
        endif()
    endforeach()
endforeach()

if(FAILURES)
    string(REPLACE ";" "\n  " report "${FAILURES}")
    message(FATAL_ERROR "Module boundary violations:\n  ${report}")
endif()

# ============================================================
# 循环依赖检查：公共头之间的 include 图不得有环
#
# 逐个公共头独立编译能捕捉到"头文件自己把依赖链走通"，但报错信息
# 往往指向深处。这里直接在 lib 层面把环找出来（如 a.h -> b.h -> a.h）。
# ============================================================
file(GLOB_RECURSE CYCLE_HEADERS "${SR_INCLUDE_DIR}/softrenderer/*.h")
foreach(header IN LISTS CYCLE_HEADERS)
    file(RELATIVE_PATH rel "${SR_INCLUDE_DIR}/softrenderer" "${header}")
    string(REPLACE "\\" "/" rel "${rel}")
    set(node "softrenderer/${rel}")
    set(deps "")
    file(STRINGS "${header}" inc_lines REGEX "^[ \t]*#[ \t]*include[ \t]*[<\"]softrenderer/")
    foreach(line IN LISTS inc_lines)
        if(line MATCHES "softrenderer/[^\">]+")
            list(APPEND deps "${CMAKE_MATCH_0}")
        endif()
    endforeach()
    set("DEPS_${node}" "${deps}")
endforeach()

function(sr_check_include_cycle node path)
    list(FIND path "${node}" seen)
    if(seen GREATER_EQUAL 0)
        string(REPLACE ";" " -> " chain "${path};${node}")
        message(FATAL_ERROR "Include cycle detected: ${chain}")
    endif()
    list(APPEND path "${node}")
    foreach(dep IN LISTS "DEPS_${node}")
        if(DEFINED "DEPS_${dep}")
            sr_check_include_cycle("${dep}" "${path}")
        endif()
    endforeach()
endfunction()

foreach(header IN LISTS CYCLE_HEADERS)
    file(RELATIVE_PATH rel "${SR_INCLUDE_DIR}/softrenderer" "${header}")
    string(REPLACE "\\" "/" rel "${rel}")
    sr_check_include_cycle("softrenderer/${rel}" "")
endforeach()

list(LENGTH HEADERS header_count)
message(STATUS "Module boundaries OK (${header_count} headers scanned, no include cycles)")
