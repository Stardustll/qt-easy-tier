/**
 * @file DaemonEndpoint.h
 * @brief daemon IPC socket 端点（平台相关）
 *
 * 集中定义前端与 qtet-daemon 通信所用的本地 socket 名称/路径，避免各处硬编码。
 *
 * 平台差异：
 *   - Linux / Windows：沿用裸名称 "qtet-daemon.sock"，由 QLocalSocket 按平台解析
 *     （Linux 下 root 与用户进程的 QDir::tempPath() 通常都落到 /tmp，语义一致）。
 *   - macOS：必须使用 root 与普通用户都能访问的**绝对路径**。因为 root（LaunchDaemon）
 *     与用户（GUI）进程的 QDir::tempPath() 不同（/tmp vs /var/folders/.../T），
 *     裸名称会让两者解析到不同的 socket 文件而无法通信。此处约定
 *     /var/run/qtet-daemon.sock，与 daemon 侧（launchd plist 注入的 --socket / QtET_DAEMON_SOCKET）
 *     保持同一字符串。
 *
 * @note 前端是 socket 路径的唯一真源：macOS 下由前端生成 launchd plist 并注入该路径。
 */
#pragma once

#include <QString>
#include <QtGlobal>

namespace DaemonEndpoint {

/// 返回当前平台 daemon IPC 端点（socket 名称或绝对路径）
inline QString socketPath()
{
#if defined(Q_OS_MACOS)
    return QStringLiteral("/var/run/qtet-daemon.sock");
#else
    return QStringLiteral("qtet-daemon.sock");
#endif
}

} // namespace DaemonEndpoint
