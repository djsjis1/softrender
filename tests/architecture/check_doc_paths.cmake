# ============================================================
# 文档路径门禁（CTest 调用：cmake -DSR_SOURCE_DIR=... -P check_doc_paths.cmake）
#
# 【为什么需要】本仓库的「当前态文档」已连续多轮出现引用了**不存在**的头文件
# （resource_manager.h / gltf_types.h / line.h / core/profiler.h / core/log.h），
# 每轮靠人工终检发现、下一轮又腐化。这里把「文档里写的头路径必须真实存在」
# 变成机器可验的门禁。
#
# 【扫描范围】只扫声明当前状态的文档，不扫历史快照：
#   docs/architecture.md、docs/modules.md、docs/project_context.md
#   .qoder/rules/*.md
# changelog 与 tasks/DONE、docs/engineering 下是历史记录，会合法提到已删除的
# 文件，纳入扫描只会制造误报。
#
# 【认哪种写法】形如 a/b.h 或 a/b/c.h 的路径，**无论是否在代码块内**都要校验
# （架构文档的依赖图就在代码块里）。判定条件三条：
#   ① 至少含一个斜杠 —— 裸文件名（`mesh.h`）在模块表里靠目录上下文才成立，
#      无上下文无法判定归属，跳过；
#   ② 不以 softrenderer/ include/ third/ 开头 —— 那是 #include 的写法或第三方；
#   ③ 解析基址依次尝试 include/softrenderer/、仓库根、src/。
# ============================================================

cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED SR_SOURCE_DIR)
    message(FATAL_ERROR "SR_SOURCE_DIR is required")
endif()

set(DOC_FILES
    "${SR_SOURCE_DIR}/docs/architecture.md"
    "${SR_SOURCE_DIR}/docs/modules.md"
    "${SR_SOURCE_DIR}/docs/project_context.md"
)
file(GLOB RULE_FILES "${SR_SOURCE_DIR}/.qoder/rules/*.md")
list(APPEND DOC_FILES ${RULE_FILES})

set(FAILURES "")
set(CHECKED 0)

foreach(doc IN LISTS DOC_FILES)
    if(NOT EXISTS "${doc}")
        continue()
    endif()
    file(READ "${doc}" content)

    # 直接抓「含斜杠的 xxx/yyy.h」：既覆盖反引号内的，也覆盖代码块内的。
    # 正则要求至少一个斜杠，天然排除裸文件名。
    string(REGEX MATCHALL "[a-z_0-9]+(/[a-z_0-9]+)+[.]h" tokens "${content}")

    set(seen "")
    foreach(token IN LISTS tokens)
        list(FIND seen "${token}" dup)
        if(NOT dup EQUAL -1)
            continue()
        endif()
        list(APPEND seen "${token}")

        # #include 写法与第三方头不按公共头路径校验；
        # path/to/xxx.h 是规则文件里的模板占位，不是真路径
        if(token MATCHES "^(softrenderer|include|third|path/to)/")
            continue()
        endif()
        list(FIND token "softrenderer/" has_prefix)
        if(NOT has_prefix EQUAL -1)
            continue()
        endif()

        math(EXPR CHECKED "${CHECKED} + 1")
        set(found FALSE)
        foreach(base "include/softrenderer" "" "src")
            if(base STREQUAL "")
                set(candidate "${SR_SOURCE_DIR}/${token}")
            else()
                set(candidate "${SR_SOURCE_DIR}/${base}/${token}")
            endif()
            if(EXISTS "${candidate}")
                set(found TRUE)
                break()
            endif()
        endforeach()
        if(NOT found)
            file(RELATIVE_PATH rel "${SR_SOURCE_DIR}" "${doc}")
            list(APPEND FAILURES "${rel}: ${token}")
        endif()
    endforeach()
endforeach()

# ---- 公共头注释里的路径引用（第一阶段：只报告，不判定失败）----
# 为什么单独一段：头注释里的「引用方 / 实现位于 xxx.cpp」连续多轮指向 2.0.0
# 迁移前的位置（stage9_msaa.cpp、core/benchmark.h、gltf_loader_impl.cpp……），
# 而 include/ 不在上面的 DOC_FILES 里，没有任何机械防线。
# 头注释里也可能合法提到历史路径，所以先只汇总打印；确认零误报后再把
# SR_HEADER_MISSES 并入 FAILURES 并删掉这段说明。
#
# 头文件本身也要把 .hpp 纳入扫描范围：将来新增的 .hpp 公共头若只扫 *.h，
# 它的注释就完全无人校验。
file(GLOB_RECURSE SR_PUBLIC_HEADERS
    "${SR_SOURCE_DIR}/include/softrenderer/*.h"
    "${SR_SOURCE_DIR}/include/softrenderer/*.hpp")
set(SR_HEADER_MISSES "")
set(SR_HEADER_CHECKED 0)
foreach(hdr IN LISTS SR_PUBLIC_HEADERS)
    file(READ "${hdr}" content)
    # 与文档同口径：至少一个斜杠才算路径（裸文件名无上下文无法判定归属）。
    # 此处比文档段宽：额外接纳 .hpp 与 .cpp（头注释会写「实现在 src/io/xxx.cpp」），
    # 路径段允许大写（将来若引入 Vec2f.h 这类名字不会被静默跳过）。
    # 扩展名后面要求跟非标识符字符，否则 entt/entt.hpp 会被截成 entt/entt.h
    # 变成假阳性；匹配到的尾随字符随后剥掉。
    string(REGEX MATCHALL "([A-Za-z_0-9]+/)+[A-Za-z_0-9]+[.](hpp|h|cpp)([^A-Za-z_0-9_]|$)" tokens "${content}")
    set(seen "")
    foreach(token IN LISTS tokens)
        string(REGEX REPLACE "[^A-Za-z_0-9_]+$" "" token "${token}")
        list(FIND seen "${token}" dup)
        if(NOT dup EQUAL -1)
            continue()
        endif()
        list(APPEND seen "${token}")
        # 第三方头目录一律跳过：注释里提到 entt/nlohmann/tinygltf/stb 是在
        # 说明用法，不是项目内文件，纳入校验只会制造必然误报。
        if(token MATCHES "^(softrenderer|include|third|path/to|entt|nlohmann|tinygltf|stb)/")
            continue()
        endif()
        math(EXPR SR_HEADER_CHECKED "${SR_HEADER_CHECKED} + 1")
        set(found FALSE)
        foreach(base "include/softrenderer" "" "src")
            if(base STREQUAL "")
                set(candidate "${SR_SOURCE_DIR}/${token}")
            else()
                set(candidate "${SR_SOURCE_DIR}/${base}/${token}")
            endif()
            if(EXISTS "${candidate}")
                set(found TRUE)
                break()
            endif()
        endforeach()
        if(NOT found)
            file(RELATIVE_PATH rel "${SR_SOURCE_DIR}" "${hdr}")
            list(APPEND SR_HEADER_MISSES "${rel}: ${token}")
        endif()
    endforeach()
endforeach()
if(SR_HEADER_MISSES)
    string(REPLACE ";" "\n  " sr_header_report "${SR_HEADER_MISSES}")
    message(STATUS
        "公共头注释引用了不存在的路径（当前只报告，不判定失败）：\n  ${sr_header_report}")
else()
    message(STATUS "Public header path references OK (${SR_HEADER_CHECKED} verified)")
endif()

# ---- README 可执行名校验 ----
# 与头路径同类的历史问题：README 引用的示例可执行名（stage4_zbuffer.exe 等）
# 与实际目标名脱节。目标名的唯一来源是 examples/CMakeLists.txt —— 两种定义
# 形态都要识别：sr_add_example(<literal>) 直呼，以及
# foreach(name IN ITEMS a b c) + sr_add_example(${name}) 循环。
# practice 目标由文件名自动生成，按 practice_ 前缀放行。
set(SR_README "${SR_SOURCE_DIR}/README.md")
set(SR_EXAMPLES_CMAKE "${SR_SOURCE_DIR}/examples/CMakeLists.txt")
if(EXISTS "${SR_README}" AND EXISTS "${SR_EXAMPLES_CMAKE}")
    file(READ "${SR_README}" sr_readme)
    file(READ "${SR_EXAMPLES_CMAKE}" sr_examples_cmake)
    set(SR_EXE_NAMES "")
    # 直呼形态
    string(REGEX MATCHALL "sr_add_example\\(([a-zA-Z0-9_]+)\\)" _sr_example_hits "${sr_examples_cmake}")
    foreach(_h IN LISTS _sr_example_hits)
        string(REGEX REPLACE "sr_add_example\\(([a-zA-Z0-9_]+)\\)" "\\1" _name "${_h}")
        if(NOT _name STREQUAL "name") # 循环形态的 ${name} 占位，跳过
            list(APPEND SR_EXE_NAMES "${_name}")
        endif()
    endforeach()
    # foreach(name IN ITEMS ...) 形态：收集所有 ITEMS 列表里的标识符
    string(REGEX MATCHALL "IN ITEMS[ \t\r\n]*[^)]*" _sr_item_blocks "${sr_examples_cmake}")
    foreach(_block IN LISTS _sr_item_blocks)
        string(REGEX MATCHALL "[a-zA-Z0-9_]+" _items "${_block}")
        list(REMOVE_ITEM _items "IN" "ITEMS" "name")
        list(APPEND SR_EXE_NAMES ${_items})
    endforeach()
    string(REGEX MATCHALL "[A-Za-z0-9_]+\\.exe" _sr_readme_exes "${sr_readme}")
    foreach(_exe IN LISTS _sr_readme_exes)
        string(REGEX REPLACE "\\.exe$" "" _name "${_exe}")
        if(_name MATCHES "^practice_")
            continue()
        endif()
        list(FIND SR_EXE_NAMES "${_name}" _idx)
        math(EXPR CHECKED "${CHECKED} + 1")
        if(_idx EQUAL -1)
            list(APPEND FAILURES "README.md: 可执行目标不存在: ${_exe}")
        endif()
    endforeach()
endif()

if(FAILURES)
    string(REPLACE ";" "\n  " report "${FAILURES}")
    message(FATAL_ERROR
        "Documentation references non-existent headers:\n  ${report}\n"
        "修正方式：把路径改成实际存在的头，或从文档中删掉该条；"
        "若确属历史记录，请移到 changelog / tasks/DONE 而不是写在当前态文档里。")
endif()

message(STATUS "Doc paths OK (${CHECKED} references verified)")
