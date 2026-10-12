function(qtet_configure_daemon)
    if(NOT BUILD_WITH_DAEMON)
        message(STATUS "qtet-daemon build disabled")
        return()
    endif()

    find_program(GIT_EXECUTABLE git)
    if(NOT GIT_EXECUTABLE)
        message(WARNING "git not found, skipping qtet-daemon build. Please place qtet-daemon runtime in the output directory manually.")
        return()
    endif()

    if(NOT DEFINED CLONE_DAEMON_FROM OR CLONE_DAEMON_FROM STREQUAL "")
        set(CLONE_DAEMON_FROM "GITHUB")
    endif()
    if(CLONE_DAEMON_FROM STREQUAL "CNB")
        # macOS 支持（0.2.0）目前仅发布在本 fork 上，原作者仓库尚未合入
        set(qtet_daemon_repo_url "https://cnb.cool/tflystar/et-daemon.asio")
    else()
        set(qtet_daemon_repo_url "https://github.com/Stardustll/et-daemon.asio.git")
    endif()
    message(STATUS "qtet-daemon clone source: ${CLONE_DAEMON_FROM} -> ${qtet_daemon_repo_url}")

    set(QTET_DAEMON_SRC_DIR "${CMAKE_BINARY_DIR}/et-daemon.asio")
    set(QTET_DAEMON_BUILD_DIR "${CMAKE_BINARY_DIR}/et-daemon.asio/build")
    set(QTET_DAEMON_OUTPUT_DIR "${CMAKE_BINARY_DIR}/et-daemon.asio/build/Output")
    if(WIN32)
        set(QTET_DAEMON_BIN_NAME "qtet-daemon.exe")
    else()
        set(QTET_DAEMON_BIN_NAME "qtet-daemon")
    endif()

    if(CMAKE_CONFIGURATION_TYPES)
        set(qtet_daemon_build_type Release)
        set(qtet_daemon_build_config Release)
    else()
        set(qtet_daemon_build_type "${CMAKE_BUILD_TYPE}")
        set(qtet_daemon_build_config "${CMAKE_BUILD_TYPE}")
    endif()
    if(qtet_daemon_build_type STREQUAL "")
        set(qtet_daemon_build_type Release)
    endif()

    set(qtet_daemon_build_command
        "${CMAKE_COMMAND}"
        "-DQTET_DAEMON_REPO_URL=${qtet_daemon_repo_url}"
        "-DQTET_DAEMON_SRC_DIR=${QTET_DAEMON_SRC_DIR}"
        "-DQTET_DAEMON_BUILD_DIR=${QTET_DAEMON_BUILD_DIR}"
        "-DQTET_DAEMON_BUILD_TYPE=${qtet_daemon_build_type}"
        "-DQTET_DAEMON_BUILD_CONFIG=${qtet_daemon_build_config}"
        "-DQTET_DAEMON_GENERATOR=${CMAKE_GENERATOR}"
        "-DQTET_DAEMON_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
        "-DQTET_DAEMON_C_COMPILER=${CMAKE_C_COMPILER}"
        "-DQTET_DAEMON_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
    )
    # QTET_DAEMON_VER 未定义时使用远端默认分支
    if(DEFINED QTET_DAEMON_VER AND NOT QTET_DAEMON_VER STREQUAL "")
        list(APPEND qtet_daemon_build_command "-DQTET_DAEMON_VER=${QTET_DAEMON_VER}")
    endif()
    if(DEFINED CMAKE_TOOLCHAIN_FILE AND NOT CMAKE_TOOLCHAIN_FILE STREQUAL "")
        list(APPEND qtet_daemon_build_command "-DQTET_DAEMON_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
    endif()
    # macOS：把 Universal 架构与部署目标透传给 qtet-daemon，保证 .app 与内置 daemon 架构一致。
    # CMAKE_OSX_ARCHITECTURES 是列表（"arm64;x86_64"）：须转义分号后作为单个 -D 参数传递，
    # 否则会被拆成两个 argv，下游 BuildDaemon.cmake 只能收到第一个架构。
    if(APPLE)
        string(REPLACE ";" "\\;" qtet_daemon_osx_arches "${CMAKE_OSX_ARCHITECTURES}")
        list(APPEND qtet_daemon_build_command
            "-DQTET_DAEMON_OSX_ARCHITECTURES=${qtet_daemon_osx_arches}"
            "-DQTET_DAEMON_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}")
    endif()
    list(APPEND qtet_daemon_build_command -P "${CMAKE_SOURCE_DIR}/cmake/scripts/BuildDaemon.cmake")

    message(STATUS "Building qtet-daemon runtime during configure")
    execute_process(
        COMMAND ${qtet_daemon_build_command}
        RESULT_VARIABLE qtet_daemon_build_result
    )
    if(NOT qtet_daemon_build_result EQUAL 0)
        message(FATAL_ERROR "Failed to build qtet-daemon runtime")
    endif()

    message(STATUS "Collecting qtet-daemon runtime into output directory during configure")
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
                -DQTET_DAEMON_OUTPUT_DIR=${QTET_DAEMON_OUTPUT_DIR}
                -DQTET_OUTPUT_DIR=${QTET_OUTPUT_DIR}
                -DQTET_DAEMON_BIN_NAME=${QTET_DAEMON_BIN_NAME}
                -P "${CMAKE_SOURCE_DIR}/cmake/scripts/CollectDaemon.cmake"
        RESULT_VARIABLE qtet_daemon_collect_result
    )
    if(NOT qtet_daemon_collect_result EQUAL 0)
        message(FATAL_ERROR "Failed to collect qtet-daemon runtime")
    endif()
endfunction()

function(qtet_attach_daemon_to_app target_name)
    # qtet-daemon is prepared during CMake configure, not as an app build dependency.
    # macOS：把已收集到 Output 的 qtet-daemon 拷进 .app/Contents/MacOS/，
    # 使 applicationDirPath()/qtet-daemon 能定位到它（DaemonRegisterHelper 依赖此约定），
    # 随后由前端在首次运行时经管理员授权安装到 /Library/PrivilegedHelperTools。
    if(APPLE AND BUILD_WITH_DAEMON AND EXISTS "${QTET_OUTPUT_DIR}/qtet-daemon")
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${QTET_OUTPUT_DIR}/qtet-daemon"
                    "$<TARGET_BUNDLE_CONTENT_DIR:${target_name}>/MacOS/qtet-daemon"
            COMMENT "Bundle qtet-daemon into ${target_name}.app/Contents/MacOS"
        )
        # daemon 经 @loader_path 解析 EasyTier FFI（@rpath/libeasytier_ffi.dylib），
        # 须与 daemon 同目录入包——安装脚本据此把它一并装到 helper 同目录，
        # 否则安装到 /Library/PrivilegedHelperTools 后 daemon 无法启动。
        if(EXISTS "${QTET_OUTPUT_DIR}/libeasytier_ffi.dylib")
            add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${QTET_OUTPUT_DIR}/libeasytier_ffi.dylib"
                        "$<TARGET_BUNDLE_CONTENT_DIR:${target_name}>/MacOS/libeasytier_ffi.dylib"
                COMMENT "Bundle libeasytier_ffi.dylib into ${target_name}.app/Contents/MacOS"
            )
        endif()
    endif()
endfunction()
