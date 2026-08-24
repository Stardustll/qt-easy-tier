cmake_minimum_required(VERSION 3.16)

project(QtEasyTier VERSION 3.1.0 LANGUAGES CXX)
set(EASYTIER_FFI_VER 2.6.4)
set(QTET_DAEMON_VER 1.0.0)

# 向 C++ 侧暴露版本号
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/AppVersion.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/AppVersion.h
    @ONLY
)

set(QTET_OUTPUT_DIR "${CMAKE_BINARY_DIR}/Output")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${QTET_OUTPUT_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${QTET_OUTPUT_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${QTET_OUTPUT_DIR}")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# 构建后端可选参数
option(BUILD_WITH_DAEMON "Build and collect qtet-daemon runtime" ON)
# qtet-daemon 克隆来源：GITEE / GITHUB / CNB，默认 GITHUB
set(CLONE_DAEMON_FROM "GITHUB" CACHE STRING "Clone qtet-daemon from GITEE, GITHUB or CNB")
# deepin DTK 构建选项，默认关闭
option(QTET_ENABLE_DTK "Build with deepin DTK support (Chameleon style + DTK main window)" OFF)
set_property(CACHE CLONE_DAEMON_FROM PROPERTY STRINGS GITEE GITHUB CNB)

include(cmake/QtEasyTierDaemon.cmake)

# deepin DTK 适配（可选）：QTET_ENABLE_DTK 显式开启，或 Linux 下自动探测 Dtk6Core/Gui/Declarative。
# 开启后：Chameleon 风格 + org.deepin.dtk 主窗口；未开启时与旧构建完全一致。
# 探测仅在用户未显式设置该选项时执行，并用 cache 变量落盘，避免遮蔽显式 -DQTET_ENABLE_DTK=OFF。
if(NOT DEFINED QTET_ENABLE_DTK AND NOT WIN32)
    find_package(Dtk6Core QUIET)
    find_package(Dtk6Gui QUIET)
    find_package(Dtk6Declarative QUIET)
    if(Dtk6Declarative_FOUND AND Dtk6Core_FOUND AND Dtk6Gui_FOUND)
        message(STATUS "Dtk6Declarative found, enabling DTK adaptation (override with -DQTET_ENABLE_DTK=OFF)")
        set(QTET_ENABLE_DTK ON CACHE BOOL "Build with deepin DTK support (Chameleon style + DTK main window)" FORCE)
    endif()
endif()

if(QTET_ENABLE_DTK)
    find_package(Dtk6Core REQUIRED)
    find_package(Dtk6Gui REQUIRED)
    find_package(Dtk6Declarative REQUIRED)
endif()

find_package(Qt6 6.8 REQUIRED COMPONENTS
    Core
    Sql
    Network
    Test
    Quick
    QuickControls2
    Widgets
    Concurrent
    Svg
    QuickDialogs2
)

if(COMMAND qt_policy)
    qt_policy(SET QTP0004 NEW)
endif()

qt_standard_project_setup(REQUIRES 6.8)

if (EXISTS "${CMAKE_CURRENT_LIST_DIR}/importedcontent/CMakeLists.txt")
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/importedcontent)
endif()

function(add_qtet_library name)
    qt_add_library(${name} STATIC ${ARGN})
    target_include_directories(${name} PUBLIC ${CMAKE_SOURCE_DIR}/src ${CMAKE_BINARY_DIR}/generated ${CMAKE_SOURCE_DIR}/ThirdParty/toml++)
endfunction()

add_subdirectory(src/core/log)
add_subdirectory(src/core/config)
add_subdirectory(src/core/favorite)
add_subdirectory(src/core/repository)
add_subdirectory(src/core/service)
add_subdirectory(src/core/util)
add_subdirectory(src/core/system_tray)
add_subdirectory(src/core/application)
add_subdirectory(src/core/vpn_manager)
add_subdirectory(src/core/viewmodel)
add_subdirectory(src/app)

if(WIN32)
    set(QTET_WINDOWS_ICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/favicon/qtet.ico")
    set(QTET_WINDOWS_RC "${CMAKE_CURRENT_BINARY_DIR}/appQtEasyTier.rc")

    if(NOT EXISTS "${QTET_WINDOWS_ICON}")
        message(FATAL_ERROR "Windows icon file not found: ${QTET_WINDOWS_ICON}")
    endif()

    file(WRITE "${QTET_WINDOWS_RC}"
        "IDI_ICON1 ICON \"${QTET_WINDOWS_ICON}\"\n"
    )
    set_source_files_properties("${QTET_WINDOWS_RC}" PROPERTIES GENERATED TRUE)
endif()

qt_add_executable(appQtEasyTier
    src/main.cpp
    assets/resources.qrc
    ${QTET_WINDOWS_RC}
)

target_include_directories(appQtEasyTier PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_BINARY_DIR}/generated ${CMAKE_SOURCE_DIR}/ThirdParty/toml++)

set_target_properties(appQtEasyTier PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

qtet_configure_daemon()
qtet_attach_daemon_to_app(appQtEasyTier)

target_link_libraries(appQtEasyTier PRIVATE
    Qt6::Core
    Qt6::Sql
    Qt6::Network
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Widgets
    Qt6::Concurrent
    Qt6::Svg
    Qt6::QuickDialogs2
    qtet_appsupport
)

if(QTET_ENABLE_DTK)
    target_link_libraries(appQtEasyTier PRIVATE Dtk6::Core Dtk6::Gui Dtk6::Declarative)
    target_compile_definitions(appQtEasyTier PRIVATE QTET_ENABLE_DTK=1)
    target_compile_definitions(qtet_appsupport PRIVATE QTET_ENABLE_DTK=1)
    # DtkIntegration.cpp 编译需要 dtkgui 头文件 include 路径
    target_link_libraries(qtet_appsupport PRIVATE Dtk6::Gui)
    # DtkIntegration.cpp 的 <QQuickStyle> 头文件属于 QtQuickControls2 模块
    target_link_libraries(qtet_appsupport PRIVATE Qt6::QuickControls2)
endif()

set(QTET_QML_FILES
    src/qml/components/Sidebar.qml
    src/qml/components/SidebarItem.qml
    src/qml/components/PageContainer.qml
    src/qml/components/Card.qml
    src/qml/components/IconToolButton.qml
    src/qml/components/EditableList.qml
    src/qml/components/EditableListItem.qml
    src/qml/components/QtETabWidget.qml
    src/qml/components/ServerListItem.qml
    src/qml/components/ProxyNetworkListItem.qml
    src/qml/components/ImportNodesDialog.qml
    src/qml/components/Theme.qml
    src/qml/pages/InstanceList.qml
    src/qml/pages/NetworkPage.qml
    src/qml/pages/NetworkOptions.qml
    src/qml/pages/NetworkRuningStatus.qml
    src/qml/pages/SettingsPage.qml
    src/qml/pages/FavoriteNodesPage.qml
    src/qml/pages/LogPage.qml
)

if(QTET_ENABLE_DTK)
    # DTK 构建使用 org.deepin.dtk 主窗口，资源别名仍为 Main.qml（main.cpp 加载路径不变）
    list(APPEND QTET_QML_FILES src/qml/MainDeepin.qml)
else()
    list(APPEND QTET_QML_FILES src/qml/Main.qml)
endif()

foreach(qml_file IN LISTS QTET_QML_FILES)
    file(RELATIVE_PATH qml_resource_alias "${CMAKE_CURRENT_SOURCE_DIR}/src/qml" "${CMAKE_CURRENT_SOURCE_DIR}/${qml_file}")
    if(qml_resource_alias STREQUAL "MainDeepin.qml")
        set(qml_resource_alias "Main.qml")
    endif()
    set_source_files_properties(${qml_file} PROPERTIES QT_RESOURCE_ALIAS "${qml_resource_alias}")
endforeach()

qt_add_qml_module(appQtEasyTier
    URI QtEasyTier
    VERSION 1.0
    RESOURCE_PREFIX "/"
    QML_FILES ${QTET_QML_FILES}
)

enable_testing()
add_subdirectory(tests)
