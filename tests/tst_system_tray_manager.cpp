#include <QTest>
#include <QWindow>

#include "config/ConfigRunState.h"
#include "daemon_service/DaemonClient.h"
#include "platform/MacDockVisibility.h"
#include "system_tray/SystemTrayManager.h"

#if defined(Q_OS_MACOS)
// 经 Objective-C 运行时读取 NSApplication 当前激活策略，用于校验 Dock 显隐是否生效。
// Regular = 0（有 Dock 图标），Accessory = 1（无 Dock 图标）。
#  include <objc/message.h>
#  include <objc/runtime.h>
namespace {
long currentActivationPolicy()
{
    Class appClass = objc_getClass("NSApplication");
    if (!appClass)
        return -1;
    id app = reinterpret_cast<id (*)(id, SEL)>(objc_msgSend)(
        reinterpret_cast<id>(appClass), sel_registerName("sharedApplication"));
    if (!app)
        return -1;
    return reinterpret_cast<long (*)(id, SEL)>(objc_msgSend)(app, sel_registerName("activationPolicy"));
}
} // namespace
#endif

class TestSystemTrayManager : public QObject {
    Q_OBJECT

private slots:
    void disconnectedDaemonUsesRedIconAndClearsRunningConfigs()
    {
        SystemTrayManager manager;
        manager.setDaemonConnectionState(DaemonClient::ConnectionState::Connected);
        manager.setConfigRunState(QStringLiteral("net-a"), ConfigRunState::Running);
        QCOMPARE(manager.runningConfigCount(), 1);
        QCOMPARE(manager.currentIconPath(), QStringLiteral(":/icons/qtet-green.png"));

        manager.setDaemonConnectionState(DaemonClient::ConnectionState::Disconnected);

        QCOMPARE(manager.runningConfigCount(), 0);
        QCOMPARE(manager.daemonStatusText(), QStringLiteral("未连接"));
        QCOMPARE(manager.daemonStatusMenuText(), QStringLiteral("🔴 后端状态：未连接"));
        QCOMPARE(manager.networkCountMenuText(), QStringLiteral("🟠 网络连接：0 个"));
        QCOMPARE(manager.currentIconPath(), QStringLiteral(":/icons/qtet-red.png"));
    }

    void connectedDaemonWithoutRunningConfigsUsesDefaultIcon()
    {
        SystemTrayManager manager;
        manager.setDaemonConnectionState(DaemonClient::ConnectionState::Connected);

        QCOMPARE(manager.runningConfigCount(), 0);
        QCOMPARE(manager.daemonStatusText(), QStringLiteral("已连接"));
        QCOMPARE(manager.daemonStatusMenuText(), QStringLiteral("🟢 后端状态：已连接"));
        QCOMPARE(manager.networkCountMenuText(), QStringLiteral("🟠 网络连接：0 个"));
        QCOMPARE(manager.currentIconPath(), QStringLiteral(":/icons/qtet.png"));
    }

    void onlyRunningStateIsCountedAsNetworkConnection()
    {
        SystemTrayManager manager;
        manager.setDaemonConnectionState(DaemonClient::ConnectionState::Connected);

        manager.setConfigRunState(QStringLiteral("starting"), ConfigRunState::Starting);
        manager.setConfigRunState(QStringLiteral("stopping"), ConfigRunState::Stopping);
        manager.setConfigRunState(QStringLiteral("running"), ConfigRunState::Running);

        QCOMPARE(manager.runningConfigCount(), 1);
        QCOMPARE(manager.networkCountMenuText(), QStringLiteral("🟢 网络连接：1 个"));
        QCOMPARE(manager.currentIconPath(), QStringLiteral(":/icons/qtet-green.png"));

        manager.setConfigRunState(QStringLiteral("running"), ConfigRunState::Stopped);
        QCOMPARE(manager.runningConfigCount(), 0);
        QCOMPARE(manager.networkCountMenuText(), QStringLiteral("🟠 网络连接：0 个"));
        QCOMPARE(manager.currentIconPath(), QStringLiteral(":/icons/qtet.png"));
    }

    void connectingDaemonUsesRedIconEvenWithRunningConfigs()
    {
        SystemTrayManager manager;
        manager.setDaemonConnectionState(DaemonClient::ConnectionState::Connected);
        manager.setConfigRunState(QStringLiteral("net-a"), ConfigRunState::Running);

        manager.setDaemonConnectionState(DaemonClient::ConnectionState::Connecting);

        QCOMPARE(manager.daemonStatusText(), QStringLiteral("连接中"));
        QCOMPARE(manager.daemonStatusMenuText(), QStringLiteral("🟠 后端状态：连接中"));
        QCOMPARE(manager.networkCountMenuText(), QStringLiteral("🟠 网络连接：0 个"));
        QCOMPARE(manager.currentIconPath(), QStringLiteral(":/icons/qtet-red.png"));
    }

    void destroyedMainWindowDoesNotCrashTrayManagerDestruction()
    {
        auto *manager = new SystemTrayManager;
        auto *window = new QWindow;
        manager->setMainWindow(window);

        delete window;
        delete manager;
    }

    void showAfterMainWindowDestroyedIsSafe()
    {
        SystemTrayManager manager;
        auto *window = new QWindow;
        manager.setMainWindow(window);

        delete window;
        manager.showMainWindowFromTray();
    }

    /// 测试目标: macOS 下隐藏/恢复 Dock 图标确实切换 NSApplication 激活策略
    void macDockVisibilityTogglesActivationPolicy()
    {
#if defined(Q_OS_MACOS)
        MacDockVisibility::setDockIconVisible(false);
        QCOMPARE(currentActivationPolicy(), 1); // Accessory：无 Dock 图标
        MacDockVisibility::setDockIconVisible(true);
        QCOMPARE(currentActivationPolicy(), 0); // Regular：恢复 Dock 图标
#else
        QSKIP("Dock 图标显隐仅适用于 macOS");
#endif
    }
};

QTEST_MAIN(TestSystemTrayManager)
#include "tst_system_tray_manager.moc"
