/**
 * @file tst_app_services.cpp
 * @brief 应用服务装配层模块的单元测试。测试内容：AppServices 构造后暴露必需单例、错误与导入通知经装配层转发到系统托盘。
 */
#include <QCoreApplication>
#include <QSqlDatabase>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include "app/AppServices.h"
#include "log/LogDispatcher.h"
#include "sqlite_repository/DatabaseConnection.h"
#include "system_tray/TrayMessageDispatcher.h"
#include "system_tray/TrayMessageSink.h"
#include "core/viewmodels/AppState.h"
#include "core/viewmodels/FavoriteNodeViewModel.h"

class RecordingTrayMessageSink final : public TrayMessageSink {
public:
    void showTrayMessage(const TrayMessage &message) override
    {
        messages.append(message);
    }

    QList<TrayMessage> messages;
};

class TestAppServices : public QObject {
    Q_OBJECT

private slots:
    void cleanup()
    {
        TrayMessageDispatcher::instance()->clearSinks();
        // 日志分发器同理只持有裸指针：用例结束后 AppServices 已析构，若不清空，
        // 下一个用例构造 AppServices 时（托盘不可用会打 Warning 日志）就会命中已释放的 sink
        LogDispatcher::instance()->clearSinks();
        // 退出拦截用例会临时开启测试模式以隔离真实配置目录；恢复放在 cleanup 而非用例内，
        // 保证断言失败提前 return 时也能复位
        QStandardPaths::setTestModeEnabled(false);
    }

    /// 测试目标：验证 AppServices 构造后有必需的 singleton 可用
    void constructedServicesExposeRequiredSingletons()
    {
        AppServices services(QSqlDatabase(), nullptr, AppServices::SkipDaemonConnection);

        // 检查各核心服务非空
        QVERIFY(services.appState());
        QVERIFY(services.settingsViewModel());
        QVERIFY(services.favoriteNodeViewModel() == nullptr);
        QVERIFY(services.logViewModel() == nullptr);
        QVERIFY(services.fontHelper());
        QVERIFY(services.daemonClient());
        QVERIFY(services.daemonApi());
        QVERIFY(services.backendStatusViewModel());
        QVERIFY(services.systemTrayManager());
    }

    /// 测试目标：全局错误会通过装配层转发为系统托盘错误通知
    void appStateErrorDispatchesTrayErrorMessage()
    {
        RecordingTrayMessageSink sink;
        TrayMessageDispatcher::instance()->addSink(&sink);
        AppServices services(QSqlDatabase(), nullptr, AppServices::SkipDaemonConnection);

        services.appState()->showError(QStringLiteral("保存失败"));

        QCOMPARE(sink.messages.size(), 1);
        QCOMPARE(sink.messages.first().level, TrayMessageLevel::Error);
        QCOMPARE(sink.messages.first().title, QStringLiteral("错误"));
        QCOMPARE(sink.messages.first().message, QStringLiteral("保存失败"));
    }

    /// 测试目标：收藏节点导入完成会通过 AppServices 转发为系统托盘信息通知
    void favoriteNodeImportDispatchesTrayInfoMessage()
    {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        DatabaseConnection db(QDir(tempDir.path()).filePath(QStringLiteral("app-services.db")));
        QVERIFY(db.open());

        const QString importPath = QDir(tempDir.path()).filePath(QStringLiteral("nodes.json"));
        QFile file(importPath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("[{\"uri\":\"tcp://new.example.com:11010\",\"display_name\":\"新节点\",\"publicKey\":\"\"}]");
        file.close();

        RecordingTrayMessageSink sink;
        TrayMessageDispatcher::instance()->addSink(&sink);
        AppServices services(db.database(), nullptr, AppServices::SkipDaemonConnection);

        QVERIFY(services.favoriteNodeViewModel());
        QVERIFY(services.favoriteNodeViewModel()->importNodesFromFile(QUrl::fromLocalFile(importPath).toString()));

        QCOMPARE(sink.messages.size(), 1);
        QCOMPARE(sink.messages.first().level, TrayMessageLevel::Info);
        QCOMPARE(sink.messages.first().title, QStringLiteral("节点导入完成"));
        QCOMPARE(sink.messages.first().message, QStringLiteral("已导入 1 个节点，跳过 0 个节点"));
    }

    /// 测试目标：macOS 的 Cmd+Q（application 收到的 QEvent::Quit）被拦截并转交统一退出流程，
    /// 而不是绕过退出确认直接结束进程
    void quitEventIsInterceptedAndRoutedToExitFlow()
    {
        // 隔离真实配置目录：保证 showExitPrompt 为默认值（true）且测试不写用户设置
        QStandardPaths::setTestModeEnabled(true);
        AppServices services(QSqlDatabase(), nullptr, AppServices::SkipDaemonConnection);

        bool promptInvoked = false;
        // 返回"用户取消"：退出流程到此结束，测试期间不会真的退出应用
        services.setExitPromptHandler([&promptInvoked]() {
            promptInvoked = true;
            return AppServices::ExitPromptResult{false, false};
        });

        QEvent quitEvent(QEvent::Quit);
        QCoreApplication::sendEvent(QCoreApplication::instance(), &quitEvent);

        // 事件被 ignore：平台据此取消本次终止（接受则直接退出，跳过确认与后端清理）
        QVERIFY(!quitEvent.isAccepted());
        // 请求转交 handleUserQuitRequest()，经 Qt::singleShot 延后一个事件循环执行
        QTRY_VERIFY_WITH_TIMEOUT(promptInvoked, 2000);
    }
};

QTEST_MAIN(TestAppServices)
#include "tst_app_services.moc"
