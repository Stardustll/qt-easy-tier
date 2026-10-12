/**
 * @file MacDockVisibility.cpp
 * @brief macOS Dock 图标显隐实现（其它平台为空操作）。
 */
#include "platform/MacDockVisibility.h"

#include <QtGlobal>

#if defined(Q_OS_MACOS)
// 通过 Objective-C 运行时直接调用 -[NSApplication setActivationPolicy:]，避免为本工程
// 引入 ObjC++（.mm）源文件与新的构建语言（当前 project(... LANGUAGES CXX)）。
// NSApplicationActivationPolicy 取值：
//   Regular   = 0：常规应用，显示 Dock 图标与菜单栏；
//   Accessory = 1：辅助应用，不显示 Dock 图标（窗口/托盘仍可正常使用）。
#  include <objc/message.h>
#  include <objc/runtime.h>

namespace {
constexpr long kActivationPolicyRegular = 0;
constexpr long kActivationPolicyAccessory = 1;

void applyActivationPolicy(long policy)
{
    Class appClass = objc_getClass("NSApplication");
    if (!appClass)
        return;
    id app = reinterpret_cast<id (*)(id, SEL)>(objc_msgSend)(reinterpret_cast<id>(appClass),
                                                             sel_registerName("sharedApplication"));
    if (!app)
        return;
    reinterpret_cast<void (*)(id, SEL, long)>(objc_msgSend)(app, sel_registerName("setActivationPolicy:"), policy);
}
} // namespace
#endif

void MacDockVisibility::setDockIconVisible(bool visible)
{
#if defined(Q_OS_MACOS)
    applyActivationPolicy(visible ? kActivationPolicyRegular : kActivationPolicyAccessory);
#else
    Q_UNUSED(visible);
#endif
}
