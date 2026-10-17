# 下游工程编译/运行验证；产物严格放入调用方的 build/integration 下。
function(run_checked)
    execute_process(COMMAND ${ARGV} RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Command failed (${result}): ${ARGV}\n${output}\n${error}")
    endif()
endfunction()

set(work "${SR_BINARY_DIR}/integration/${SR_MODE}-${SR_CONFIG}")
file(MAKE_DIRECTORY "${work}")
set(configure_args -G "${SR_GENERATOR}" "-DCMAKE_CXX_COMPILER=${SR_CXX_COMPILER}"
    "-DCMAKE_BUILD_TYPE=${SR_CONFIG}" "-DSR_MODE=${SR_MODE}" "-DSR_SOURCE_DIR=${SR_SOURCE_DIR}")
if(SR_PLATFORM)
    list(APPEND configure_args -A "${SR_PLATFORM}")
endif()
if(SR_TOOLSET)
    list(APPEND configure_args -T "${SR_TOOLSET}")
endif()

if(SR_MODE STREQUAL "installed")
    # 每轮用隔离的新前缀，不删除用户文件。移动整个前缀后原安装路径不再存在，
    # 可发现导出目标中意外记录的绝对安装路径。
    string(RANDOM LENGTH 10 ALPHABET 0123456789abcdef run_id)
    set(original "${work}/stage-${run_id}")
    set(relocated "${work}/relocated-${run_id}")
    run_checked("${CMAKE_COMMAND}" --install "${SR_BINARY_DIR}" --config "${SR_CONFIG}" --prefix "${original}")
    file(RENAME "${original}" "${relocated}")
    list(APPEND configure_args "-DCMAKE_PREFIX_PATH=${relocated}" "-DSoftRenderer_DIR=SoftRenderer_DIR-NOTFOUND")
    if(DEFINED SR_INSTALL_LIBDIR AND NOT SR_INSTALL_LIBDIR STREQUAL "lib")
        # 任意自定义 libdir 不属于 find_package 的默认前缀搜索规则，须指向配置文件。
        list(APPEND configure_args "-DSoftRenderer_DIR=${relocated}/${SR_INSTALL_LIBDIR}/cmake/SoftRenderer")
    endif()
endif()

run_checked("${CMAKE_COMMAND}" -S "${SR_SOURCE_DIR}/tests/integration/consumer" -B "${work}/build" ${configure_args})
run_checked("${CMAKE_COMMAND}" --build "${work}/build" --config "${SR_CONFIG}" --parallel 2)
run_checked("${CMAKE_CTEST_COMMAND}" --test-dir "${work}/build" -C "${SR_CONFIG}" --output-on-failure)
message(STATUS "Consumer ${SR_MODE}: configure, build and run passed")
