add_qtet_library(qtet_platform
    AutoStartHelper.cpp
    AutoStartHelper.h
    DaemonRegisterHelper.cpp
    DaemonRegisterHelper.h
    FontHelper.cpp
    FontHelper.h
    MacDockVisibility.cpp
    MacDockVisibility.h
)
target_link_libraries(qtet_platform PUBLIC Qt6::Core Qt6::Network Qt6::Widgets qtet_log)

if(WIN32)
    target_link_libraries(qtet_platform PUBLIC shell32)
endif()

if(APPLE)
    # MacDockVisibility 经 Objective-C 运行时调用 NSApplication，需链接 AppKit
    target_link_libraries(qtet_platform PUBLIC "-framework AppKit")
endif()
