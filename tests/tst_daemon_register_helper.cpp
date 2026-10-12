/**
 * @file tst_daemon_register_helper.cpp
 * @brief daemon 服务注册辅助工具单元测试。
 */
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include "platform/DaemonRegisterHelper.h"

class TestDaemonRegisterHelper : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
#if defined(Q_OS_LINUX)
        QVERIFY(m_tempDir.isValid());
        m_servicePath = QDir(m_tempDir.path()).filePath(QStringLiteral("qtet-daemon.service"));
        m_daemonPath = QDir(m_tempDir.path()).filePath(QStringLiteral("qtet-daemon"));
        DaemonRegisterHelper::setSystemdServicePathOverrideForTesting(m_servicePath);
        DaemonRegisterHelper::setDaemonBinaryPathOverrideForTesting(m_daemonPath);
#elif defined(Q_OS_WIN)
        QVERIFY(m_tempDir.isValid());
        m_daemonPath = QDir(m_tempDir.path()).filePath(QStringLiteral("qtet-daemon.exe"));
        DaemonRegisterHelper::setDaemonBinaryPathOverrideForTesting(m_daemonPath);
#elif defined(Q_OS_MACOS)
        // "服务文件"在 macOS 上为 LaunchDaemon plist；用临时目录隔离，避免触碰 /Library
        QVERIFY(m_tempDir.isValid());
        m_servicePath = QDir(m_tempDir.path()).filePath(QStringLiteral("cn.qtet.qteasytier.daemon.plist"));
        m_daemonPath = QDir(m_tempDir.path()).filePath(QStringLiteral("qtet-daemon"));
        // requiredAction() 在 macOS 下还会校验 daemon 依赖的 EasyTier FFI 动态库，
        // 预先放置同名占位文件（lib 路径由 daemon 路径所在目录推导）
        m_libPath = QDir(m_tempDir.path()).filePath(QStringLiteral("libeasytier_ffi.dylib"));
        QFile libFile(m_libPath);
        QVERIFY(libFile.open(QIODevice::WriteOnly));
        libFile.write("placeholder");
        libFile.close();
        DaemonRegisterHelper::setLaunchDaemonPlistPathOverrideForTesting(m_servicePath);
        DaemonRegisterHelper::setDaemonBinaryPathOverrideForTesting(m_daemonPath);
#else
        QSKIP("DaemonRegisterHelper service tests require Linux, Windows or macOS");
#endif
    }

    void cleanupTestCase()
    {
#if defined(Q_OS_LINUX)
        DaemonRegisterHelper::setSystemdServicePathOverrideForTesting(QString());
        DaemonRegisterHelper::setDaemonBinaryPathOverrideForTesting(QString());
        DaemonRegisterHelper::setDaemonProcessRunningOverrideForTesting(false, false);
#elif defined(Q_OS_WIN)
        DaemonRegisterHelper::setDaemonBinaryPathOverrideForTesting(QString());
        DaemonRegisterHelper::setServiceRegisteredOverrideForTesting(false, false);
        DaemonRegisterHelper::setDaemonProcessRunningOverrideForTesting(false, false);
#elif defined(Q_OS_MACOS)
        DaemonRegisterHelper::setLaunchDaemonPlistPathOverrideForTesting(QString());
        DaemonRegisterHelper::setDaemonBinaryPathOverrideForTesting(QString());
        DaemonRegisterHelper::setServiceRegisteredOverrideForTesting(false, false);
        DaemonRegisterHelper::setDaemonProcessRunningOverrideForTesting(false, false);
#endif
    }

    void isServiceRegistered_returnsFalseWhenServiceFileMissing()
    {
#if defined(Q_OS_LINUX) || defined(Q_OS_MACOS)
        QFile::remove(m_servicePath);
        QVERIFY(!DaemonRegisterHelper::isServiceRegistered());
#else
        QSKIP("isServiceRegistered 仅适用于 Linux/macOS");
#endif
    }

    void isServiceRegistered_returnsTrueWhenServiceFileExists()
    {
#if defined(Q_OS_LINUX) || defined(Q_OS_MACOS)
        QFile file(m_servicePath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write("[Unit]\nDescription=EasyTier Service\n");
        file.close();

        QVERIFY(DaemonRegisterHelper::isServiceRegistered());
#else
        QSKIP("isServiceRegistered 仅适用于 Linux/macOS");
#endif
    }

    void ensureDaemonService_returnsMissingWhenDaemonBinaryMissing()
    {
#if defined(Q_OS_LINUX)
        QFile::remove(m_daemonPath);
        QCOMPARE(DaemonRegisterHelper::ensureDaemonService(), DaemonRegisterHelper::EnsureResult::DaemonBinaryMissing);
#elif defined(Q_OS_WIN)
        QFile::remove(m_daemonPath);
        QCOMPARE(DaemonRegisterHelper::ensureDaemonService(), DaemonRegisterHelper::EnsureResult::DaemonBinaryMissing);
#elif defined(Q_OS_MACOS)
        QFile::remove(m_daemonPath);
        QCOMPARE(DaemonRegisterHelper::ensureDaemonService(), DaemonRegisterHelper::EnsureResult::DaemonBinaryMissing);
#endif
    }

    void requiredAction_returnsMissingWhenFfiLibraryMissing()
    {
#if defined(Q_OS_MACOS)
        QFile daemonFile(m_daemonPath);
        QVERIFY(daemonFile.open(QIODevice::WriteOnly));
        daemonFile.close();
        QVERIFY(daemonFile.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));

        // 移除 FFI 动态库：daemon 无法加载，requiredAction 提前判定为二进制缺失
        QVERIFY(QFile::exists(m_libPath));
        QVERIFY(QFile::remove(m_libPath));
        QCOMPARE(DaemonRegisterHelper::requiredAction(),
                 DaemonRegisterHelper::RequiredAction::DaemonBinaryMissing);

        // 恢复占位库，避免影响后续用例
        QFile restore(m_libPath);
        QVERIFY(restore.open(QIODevice::WriteOnly));
        restore.write("placeholder");
        restore.close();
#else
        QSKIP("FFI 动态库校验仅适用于 macOS");
#endif
    }

    void requiredAction_returnsRegisterServiceWhenServiceFileMissing()
    {
#if defined(Q_OS_LINUX) || defined(Q_OS_MACOS)
        QFile daemonFile(m_daemonPath);
        QVERIFY(daemonFile.open(QIODevice::WriteOnly));
        daemonFile.close();
        QVERIFY(daemonFile.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        QFile::remove(m_servicePath);

        QCOMPARE(DaemonRegisterHelper::requiredAction(), DaemonRegisterHelper::RequiredAction::RegisterService);
#elif defined(Q_OS_WIN)
        QFile daemonFile(m_daemonPath);
        QVERIFY(daemonFile.open(QIODevice::WriteOnly));
        daemonFile.close();
        DaemonRegisterHelper::setServiceRegisteredOverrideForTesting(true, false);

        QCOMPARE(DaemonRegisterHelper::requiredAction(), DaemonRegisterHelper::RequiredAction::RegisterService);
#endif
    }

    void requiredAction_returnsStartServiceWhenRegisteredButProcessNotRunning()
    {
#if defined(Q_OS_LINUX) || defined(Q_OS_MACOS)
        QFile daemonFile(m_daemonPath);
        QVERIFY(daemonFile.open(QIODevice::WriteOnly));
        daemonFile.close();
        QVERIFY(daemonFile.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));

        QFile serviceFile(m_servicePath);
        QVERIFY(serviceFile.open(QIODevice::WriteOnly | QIODevice::Text));
        serviceFile.write("[Unit]\nDescription=EasyTier Service\n");
        serviceFile.close();
        DaemonRegisterHelper::setDaemonProcessRunningOverrideForTesting(true, false);

        QCOMPARE(DaemonRegisterHelper::requiredAction(), DaemonRegisterHelper::RequiredAction::StartService);
#elif defined(Q_OS_WIN)
        QFile daemonFile(m_daemonPath);
        QVERIFY(daemonFile.open(QIODevice::WriteOnly));
        daemonFile.close();
        DaemonRegisterHelper::setServiceRegisteredOverrideForTesting(true, true);
        DaemonRegisterHelper::setDaemonProcessRunningOverrideForTesting(true, false);

        QCOMPARE(DaemonRegisterHelper::requiredAction(), DaemonRegisterHelper::RequiredAction::StartService);
#endif
    }

    void requiredAction_returnsNoneWhenRegisteredAndProcessRunning()
    {
#if defined(Q_OS_LINUX) || defined(Q_OS_MACOS)
        QFile daemonFile(m_daemonPath);
        QVERIFY(daemonFile.open(QIODevice::WriteOnly));
        daemonFile.close();
        QVERIFY(daemonFile.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));

        QFile serviceFile(m_servicePath);
        QVERIFY(serviceFile.open(QIODevice::WriteOnly | QIODevice::Text));
        serviceFile.write("[Unit]\nDescription=EasyTier Service\n");
        serviceFile.close();
        DaemonRegisterHelper::setDaemonProcessRunningOverrideForTesting(true, true);

        QCOMPARE(DaemonRegisterHelper::requiredAction(), DaemonRegisterHelper::RequiredAction::None);
#elif defined(Q_OS_WIN)
        QFile daemonFile(m_daemonPath);
        QVERIFY(daemonFile.open(QIODevice::WriteOnly));
        daemonFile.close();
        DaemonRegisterHelper::setServiceRegisteredOverrideForTesting(true, true);
        DaemonRegisterHelper::setDaemonProcessRunningOverrideForTesting(true, true);

        QCOMPARE(DaemonRegisterHelper::requiredAction(), DaemonRegisterHelper::RequiredAction::None);
#endif
    }

    void daemonBinaryPath_returnsWindowsDaemonPath()
    {
#if defined(Q_OS_WIN)
        QCOMPARE(DaemonRegisterHelper::daemonBinaryPath(), m_daemonPath);
#else
        QSKIP("daemonBinaryPath 仅适用于 Windows");
#endif
    }

    void daemonBinaryPath_returnsOverrideOnMacOS()
    {
#if defined(Q_OS_MACOS)
        QCOMPARE(DaemonRegisterHelper::daemonBinaryPath(), m_daemonPath);
#else
        QSKIP("daemonBinaryPath override 仅适用于 macOS");
#endif
    }

    void uninstallDaemonService_returnsFalseWhenNotRegistered()
    {
#if defined(Q_OS_LINUX) || defined(Q_OS_MACOS)
        // 服务文件不存在 → 未注册 → 直接返回 false，不触发提权
        QFile::remove(m_servicePath);
        QVERIFY(!DaemonRegisterHelper::uninstallDaemonService());
#elif defined(Q_OS_WIN)
        // 未注册 → 直接返回 false，不触发提权
        DaemonRegisterHelper::setServiceRegisteredOverrideForTesting(true, false);
        QVERIFY(!DaemonRegisterHelper::uninstallDaemonService());
        DaemonRegisterHelper::setServiceRegisteredOverrideForTesting(false, false);
#endif
    }

    void systemdServiceContent_containsDaemonPathAndWorkingDirectory()
    {
#if defined(Q_OS_LINUX)
        const QString content = DaemonRegisterHelper::systemdServiceContentForTesting(m_daemonPath);

        QVERIFY(content.contains(QStringLiteral("ExecStart=%1").arg(m_daemonPath)));
        QVERIFY(content.contains(QStringLiteral("WorkingDirectory=%1").arg(QFileInfo(m_daemonPath).absolutePath())));
        QVERIFY(content.contains(QStringLiteral("Restart=always")));
#else
        QSKIP("systemd 服务内容仅适用于 Linux");
#endif
    }

    void launchDaemonPlist_containsLabelHelperAndKeepAlive()
    {
#if defined(Q_OS_MACOS)
        const QString content = DaemonRegisterHelper::launchDaemonPlistContentForTesting(m_daemonPath);

        // 引用的是传入的（安装后）helper 绝对路径
        QVERIFY(content.contains(m_daemonPath));
        QVERIFY(content.contains(QStringLiteral("cn.qtet.qteasytier.daemon")));
        QVERIFY(content.contains(QStringLiteral("<key>KeepAlive</key>")));
        QVERIFY(content.contains(QStringLiteral("<key>RunAtLoad</key>")));
        // socket 路径与前端 DaemonEndpoint::socketPath() 保持一致
        QVERIFY(content.contains(QStringLiteral("/var/run/qtet-daemon.sock")));
#else
        QSKIP("LaunchDaemon plist 内容仅适用于 macOS");
#endif
    }

    void installScript_installsHelperAndFfiLibrary()
    {
#if defined(Q_OS_MACOS)
        const QString libPath = QDir(m_tempDir.path()).filePath(QStringLiteral("libeasytier_ffi.dylib"));
        const QString plistPath = QDir(m_tempDir.path()).filePath(QStringLiteral("daemon.plist"));
        const QString content = DaemonRegisterHelper::installScriptContentForTesting(m_daemonPath, libPath, plistPath);

        // 以 root 安装 helper 到稳定路径 + 注册 LaunchDaemon
        QVERIFY(content.contains(QStringLiteral("/Library/PrivilegedHelperTools/cn.qtet.qteasytier.daemon")));
        QVERIFY(content.contains(QStringLiteral("launchctl bootstrap system")));
        // 传入的 daemon 源路径与 plist 源路径均被引用
        QVERIFY(content.contains(m_daemonPath));
        QVERIFY(content.contains(plistPath));
        // FFI 动态库（daemon 经 @loader_path 加载）须与 helper 同目录安装
        QVERIFY(content.contains(libPath));
        QVERIFY(content.contains(QStringLiteral("/Library/PrivilegedHelperTools/libeasytier_ffi.dylib")));
        // 源缺失时跳过安装（静态链接 FFI 的兼容分支）
        QVERIFY(content.contains(QStringLiteral("if [ -f ")));
#else
        QSKIP("安装脚本内容仅适用于 macOS");
#endif
    }

private:
    QTemporaryDir m_tempDir;
    QString m_servicePath;
    QString m_daemonPath;
    QString m_libPath;
};

QTEST_MAIN(TestDaemonRegisterHelper)
#include "tst_daemon_register_helper.moc"
