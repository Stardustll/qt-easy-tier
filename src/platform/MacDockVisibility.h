/**
 * @file MacDockVisibility.h
 * @brief 按需显示/隐藏 macOS Dock 图标的平台辅助接口。
 *
 * 主窗口可见时保留 Dock 图标（常规应用），隐藏到托盘后移除 Dock 图标，
 * 使隐藏态下 Dock 与应用切换器不再残留条目，仅托盘图标常驻。
 */
#pragma once

namespace MacDockVisibility {
/**
 * @brief 设置是否在 macOS Dock 中显示本应用图标。
 *
 * macOS 下通过切换 NSApplication 的激活策略实现：@p visible 为 false 时切到
 * Accessory（无 Dock 图标，窗口与托盘仍可用），为 true 时恢复 Regular。
 * 其它平台为空操作。
 */
void setDockIconVisible(bool visible);
} // namespace MacDockVisibility
