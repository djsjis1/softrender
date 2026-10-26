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

if(FAILURES)
    string(REPLACE ";" "\n  " report "${FAILURES}")
    message(FATAL_ERROR
        "Documentation references non-existent headers:\n  ${report}\n"
        "修正方式：把路径改成实际存在的头，或从文档中删掉该条；"
        "若确属历史记录，请移到 changelog / tasks/DONE 而不是写在当前态文档里。")
endif()

message(STATUS "Doc paths OK (${CHECKED} header references verified)")
