/**
 * @file DaemonRegisterHelper.cpp
 * @brief daemon 系统服务注册辅助工具类实现
 */
#include "DaemonRegisterHelper.h"
#include "log/LogHelper.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTextStream>

#if defined(Q_OS_WIN)
#include <qt_windows.h>
#include <shellapi.h>
#ifdef StartService
#undef StartService
#endif
#endif

namespace {
constexpr char kServiceName[] = "qtet-daemon.service";
constexpr char kDaemonName[] = "qtet-daemon";
constexpr char kWindowsServiceName[] = "qtet-daemon.sock";
constexpr char kWindowsDaemonName[] = "qtet-daemon.exe";
constexpr int kProcessTimeoutMs = 30000;

#if defined(Q_OS_MACOS)
// macOS LaunchDaemon 约定（与前端 DaemonEndpoint::socketPath() 保持一致）
constexpr char kMacLaunchDaemonLabel[] = "cn.qtet.qteasytier.daemon";
constexpr char kMacLaunchDaemonPlistPath[] = "/Library/LaunchDaemons/cn.qtet.qteasytier.daemon.plist";
constexpr char kMacHelperInstallPath[] = "/Library/PrivilegedHelperTools/cn.qtet.qteasytier.daemon";
constexpr char kMacSocketPath[] = "/var/run/qtet-daemon.sock";
// qtet-daemon 以 LC_LOAD_DYLIB=@rpath/libeasytier_ffi.dylib + RPATH @loader_path
// 加载 EasyTier FFI，故该动态库须与 helper 同目录安装；文件名固定（install name 即此名）
constexpr char kMacFfiLibName[] = "libeasytier_ffi.dylib";
constexpr char kMacFfiLibInstallPath[] = "/Library/PrivilegedHelperTools/libeasytier_ffi.dylib";
#endif

#if defined(Q_OS_LINUX) || defined(Q_OS_WIN) || defined(Q_OS_MACOS)
QString g_servicePathOverride;
QString g_daemonPathOverride;
bool g_serviceRegisteredOverrideEnabled = false;
bool g_serviceRegisteredOverride = false;
bool g_processRunningOverrideEnabled = false;
bool g_processRunningOverride = false;
#endif

#if defined(Q_OS_LINUX)
QString systemdServicePath()
{
    if (!g_servicePathOverride.isEmpty())
        return g_servicePathOverride;
    return QStringLiteral("/etc/systemd/system/%1").arg(QLatin1String(kServiceName));
}

QString linuxDaemonBinaryPath()
{
    if (!g_daemonPathOverride.isEmpty())
        return g_daemonPathOverride;
    return QDir(QCoreApplication::applicationDirPath()).filePath(QLatin1String(kDaemonName));
}
#endif // Linux 专用（systemd 路径解析）

// ========== 进程执行工具（平台无关，各平台分支复用） ==========
// 捕获 stdout+stderr 并返回退出码是否为 0：Windows 查询服务状态、macOS 查询 launchd
// 服务状态等需要读取输出的场景使用。
bool runProcessWithOutput(const QString &program, const QStringList &arguments, QString *output,
                          int timeoutMs = kProcessTimeoutMs)
{
    QProcess process;
    process.start(program, arguments);
    if (!process.waitForStarted(timeoutMs))
        return false;
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished();
        return false;
    }
    if (output)
        *output = QString::fromLocal8Bit(process.readAllStandardOutput())
                  + QString::fromLocal8Bit(process.readAllStandardError());
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

// ========== 进程执行与 shell 引用工具（Linux 与 macOS 共用） ==========
#if defined(Q_OS_LINUX) || defined(Q_OS_MACOS)
bool runProcess(const QString &program, const QStringList &arguments, int timeoutMs = kProcessTimeoutMs)
{
    QProcess process;
    process.start(program, arguments);
    if (!process.waitForStarted(timeoutMs))
        return false;
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished();
        return false;
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

// systemd 专用转义：非 Linux 平台不引用，标记 maybe_unused 避免告警
[[maybe_unused]] QString systemdEscapePath(QString path)
{
    path.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    path.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return path;
}

QString shellQuote(QString value)
{
    value.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QStringLiteral("'%1'").arg(value);
}

#endif // 共用工具结束

// ========== Linux 专用：systemd 服务生成与 pkexec 提权 ==========
#if defined(Q_OS_LINUX)
QString serviceContent(const QString &daemonPath)
{
    const QFileInfo daemonInfo(daemonPath);
    const QString execStart = systemdEscapePath(daemonInfo.absoluteFilePath());
    const QString workingDirectory = systemdEscapePath(daemonInfo.absolutePath());

    QString content;
    QTextStream stream(&content);
    stream << "[Unit]\n";
    stream << "Description=EasyTier Service\n";
    stream << "After=network.target network-online.target syslog.target\n";
    stream << "Wants=network.target network-online.target\n";
    stream << "\n";
    stream << "[Service]\n";
    stream << "Type=simple\n";
    stream << "ExecStart=" << execStart << "\n";
    stream << "WorkingDirectory=" << workingDirectory << "\n";
    stream << "Restart=always\n";
    stream << "RestartSec=3s\n";
    stream << "StartLimitIntervalSec=0\n";
    stream << "\n";
    stream << "[Install]\n";
    stream << "WantedBy=multi-user.target\n";
    return content;
}

bool writeTemporaryServiceFile(QTemporaryFile &file, const QString &daemonPath)
{
    if (!file.open())
        return false;

    QTextStream stream(&file);
    stream << serviceContent(daemonPath);
    stream.flush();
    if (stream.status() != QTextStream::Ok)
        return false;

    file.close();
    return file.error() == QFileDevice::NoError;
}

bool installAndStartService(const QString &daemonPath)
{
    QTemporaryFile serviceFile(QDir::tempPath() + QStringLiteral("/qtet-daemon-service-XXXXXX.service"));
    serviceFile.setAutoRemove(true);
    if (!writeTemporaryServiceFile(serviceFile, daemonPath)) {
        LogHelper::logWarning(QStringLiteral("生成 qtet-daemon systemd service 临时文件失败"), "DaemonRegister");
        return false;
    }

    const QString command = QStringLiteral("install -m 0644 %1 %2 && systemctl daemon-reload && systemctl enable %3 && systemctl start %3")
                                .arg(shellQuote(serviceFile.fileName()),
                                     shellQuote(systemdServicePath()),
                                     shellQuote(QLatin1String(kServiceName)));
    return runProcess(QStringLiteral("pkexec"), {QStringLiteral("sh"), QStringLiteral("-c"), command}, 120000);
}

bool startService()
{
    return runProcess(QStringLiteral("pkexec"), {QStringLiteral("systemctl"), QStringLiteral("start"), QLatin1String(kServiceName)}, 120000);
}

bool uninstallService()
{
    // 停止、禁用并移除服务文件，最后重载 systemd 配置。
    // 停止/禁用失败不阻断后续步骤，最终以 daemon-reload 的成败为准。
    const QString command = QStringLiteral("systemctl stop %1 2>/dev/null; systemctl disable %1 2>/dev/null; rm -f %2; systemctl daemon-reload")
                                .arg(shellQuote(QLatin1String(kServiceName)),
                                     shellQuote(systemdServicePath()));
    return runProcess(QStringLiteral("pkexec"), {QStringLiteral("sh"), QStringLiteral("-c"), command}, 120000);
}
#endif

#if defined(Q_OS_WIN)
QString windowsDaemonBinaryPath()
{
    if (!g_daemonPathOverride.isEmpty())
        return g_daemonPathOverride;
    return QDir(QCoreApplication::applicationDirPath()).filePath(QLatin1String(kWindowsDaemonName));
}

bool runElevated(const QString &program, const QString &parameters, int timeoutMs = 120000)
{
    SHELLEXECUTEINFOW shellExecuteInfo = {};
    shellExecuteInfo.cbSize = sizeof(shellExecuteInfo);
    shellExecuteInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
    shellExecuteInfo.lpVerb = L"runas";

    const std::wstring programW = QDir::toNativeSeparators(program).toStdWString();
    const std::wstring parametersW = parameters.toStdWString();
    shellExecuteInfo.lpFile = programW.c_str();
    shellExecuteInfo.lpParameters = parametersW.c_str();
    shellExecuteInfo.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&shellExecuteInfo))
        return false;
    if (!shellExecuteInfo.hProcess)
        return true;

    const DWORD waitResult = WaitForSingleObject(shellExecuteInfo.hProcess, static_cast<DWORD>(timeoutMs));
    if (waitResult != WAIT_OBJECT_0) {
        CloseHandle(shellExecuteInfo.hProcess);
        return false;
    }

    DWORD exitCode = 1;
    const bool gotExitCode = GetExitCodeProcess(shellExecuteInfo.hProcess, &exitCode);
    CloseHandle(shellExecuteInfo.hProcess);
    return gotExitCode && exitCode == 0;
}

bool installAndStartWindowsService()
{
    const QString daemonPath = windowsDaemonBinaryPath();
    return runElevated(daemonPath, QStringLiteral("--install"))
        && runElevated(daemonPath, QStringLiteral("--start"));
}

bool startWindowsService()
{
    return runElevated(windowsDaemonBinaryPath(), QStringLiteral("--start"));
}

bool uninstallWindowsService()
{
    const QString daemonPath = windowsDaemonBinaryPath();
    // 先停止服务，再执行卸载（两个命令需分别提权执行）
    return runElevated(daemonPath, QStringLiteral("--stop"))
        && runElevated(daemonPath, QStringLiteral("--uninstall"));
}
#endif

// ========== macOS 专用：LaunchDaemon 生成与管理员授权安装 ==========
#if defined(Q_OS_MACOS)
QString macPlistPath()
{
    if (!g_servicePathOverride.isEmpty())
        return g_servicePathOverride;
    return QLatin1String(kMacLaunchDaemonPlistPath);
}

QString macDaemonBinaryPath()
{
    if (!g_daemonPathOverride.isEmpty())
        return g_daemonPathOverride;
    // CMake 在 macOS 下把 qtet-daemon 收进 .app 的 Contents/MacOS/，
    // 与 Linux/Windows 的 applicationDirPath() 解析保持一致。
    return QDir(QCoreApplication::applicationDirPath()).filePath(QLatin1String(kDaemonName));
}

// EasyTier FFI 动态库在 bundle 内的路径（与 daemon 同在 Contents/MacOS/）。
// 安装脚本据此把它装到 helper 同目录，供 daemon 经 @loader_path 加载；
// 单测以 g_daemonPathOverride 所在目录为基准。
QString macDaemonLibPath()
{
    if (!g_daemonPathOverride.isEmpty())
        return QFileInfo(g_daemonPathOverride).absoluteDir().filePath(QLatin1String(kMacFfiLibName));
    return QDir(QCoreApplication::applicationDirPath()).filePath(QLatin1String(kMacFfiLibName));
}

// 生成 LaunchDaemon plist；helperPath 为安装到 /Library 后的稳定绝对路径，
// 因此 plist 中 ProgramArguments[0] 引用的是安装位置而非 bundle 内路径。
QString launchDaemonPlistContent(const QString &helperPath)
{
    const QString installedHelper = QFileInfo(helperPath).absoluteFilePath();

    QString content;
    QTextStream stream(&content);
    stream << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    stream << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n";
    stream << "<plist version=\"1.0\">\n";
    stream << "<dict>\n";
    stream << "    <key>Label</key>\n";
    stream << "    <string>" << QLatin1String(kMacLaunchDaemonLabel) << "</string>\n";
    stream << "    <key>ProgramArguments</key>\n";
    stream << "    <array>\n";
    stream << "        <string>" << installedHelper << "</string>\n";
    stream << "        <string>--socket</string>\n";
    stream << "        <string>" << QLatin1String(kMacSocketPath) << "</string>\n";
    stream << "    </array>\n";
    stream << "    <key>RunAtLoad</key>\n";
    stream << "    <true/>\n";
    // RunAtLoad + KeepAlive ≈ systemd 的 Restart=always
    stream << "    <key>KeepAlive</key>\n";
    stream << "    <true/>\n";
    // Umask=0 让 daemon 绑定的 unix socket 对外（普通用户）可连接
    stream << "    <key>Umask</key>\n";
    stream << "    <integer>0</integer>\n";
    stream << "    <key>EnvironmentVariables</key>\n";
    stream << "    <dict>\n";
    stream << "        <key>QTET_DAEMON_SOCKET</key>\n";
    stream << "        <string>" << QLatin1String(kMacSocketPath) << "</string>\n";
    stream << "    </dict>\n";
    stream << "    <key>StandardOutPath</key>\n";
    stream << "    <string>/var/log/qteasytier-daemon.log</string>\n";
    stream << "    <key>StandardErrorPath</key>\n";
    stream << "    <string>/var/log/qteasytier-daemon.log</string>\n";
    stream << "</dict>\n";
    stream << "</plist>\n";
    return content;
}

bool writeTemporaryPlist(QTemporaryFile &file, const QString &helperPath)
{
    if (!file.open())
        return false;

    QTextStream stream(&file);
    stream << launchDaemonPlistContent(helperPath);
    stream.flush();
    if (stream.status() != QTextStream::Ok)
        return false;

    file.close();
    return file.error() == QFileDevice::NoError;
}

// 生成以 root 执行的安装脚本：安装 helper + FFI 动态库 + plist，并 bootstrap 启动
QString installScriptContent(const QString &daemonSource, const QString &libSource, const QString &plistSource)
{
    QString content;
    QTextStream stream(&content);
    stream << "#!/bin/sh\n";
    stream << "set -e\n";
    stream << "LABEL=" << QLatin1String(kMacLaunchDaemonLabel) << "\n";
    stream << "HELPER=" << QLatin1String(kMacHelperInstallPath) << "\n";
    stream << "LIB=" << QLatin1String(kMacFfiLibInstallPath) << "\n";
    stream << "PLIST=" << QLatin1String(kMacLaunchDaemonPlistPath) << "\n";
    // 清理可能存在的旧服务（失败不阻断后续步骤）
    stream << "/bin/launchctl bootout system/$LABEL 2>/dev/null || true\n";
    // 清除 launchd 持久化的 disabled 覆盖：卸载流程执行过 `launchctl disable`，该标记会
    // 持久保存在 launchd 覆盖库中；若不先清除，下面的 bootstrap 会因服务被禁用而失败，
    // 配合 set -e 会直接中止脚本，导致随后的 enable/kickstart 永不执行 —— 表现为
    // "管理员授权成功但前端连不上后端"（服务注册不上、进程起不来）。
    stream << "/bin/launchctl enable system/$LABEL 2>/dev/null || true\n";
    stream << "/usr/bin/install -d -m 0755 -o root -g wheel /Library/PrivilegedHelperTools\n";
    stream << "/usr/bin/install -m 0755 -o root -g wheel " << shellQuote(daemonSource) << " \"$HELPER\"\n";
    // 清除从 .app 继承的 quarantine 标记，否则 launchd 可能拒绝执行
    stream << "/usr/bin/xattr -cr \"$HELPER\" 2>/dev/null || true\n";
    stream << "/usr/bin/codesign --force -s - \"$HELPER\" 2>/dev/null || true\n";
    // FFI 动态库（daemon 经 @loader_path 在本目录加载）：须与 helper 同目录；源缺失则
    // 跳过（例如未来改为静态链接 FFI），避免 set -e 中断整个安装
    stream << "if [ -f " << shellQuote(libSource) << " ]; then\n";
    stream << "  /usr/bin/install -m 0644 -o root -g wheel " << shellQuote(libSource) << " \"$LIB\"\n";
    stream << "  /usr/bin/xattr -cr \"$LIB\" 2>/dev/null || true\n";
    stream << "  /usr/bin/codesign --force -s - \"$LIB\" 2>/dev/null || true\n";
    stream << "fi\n";
    stream << "/usr/bin/install -m 0644 -o root -g wheel " << shellQuote(plistSource) << " \"$PLIST\"\n";
    stream << "/usr/bin/xattr -cr \"$PLIST\" 2>/dev/null || true\n";
    // bootstrap 在"服务已加载"时会返回非零，不应因此中止脚本；最终以 kickstart 的成败为准
    stream << "/bin/launchctl bootstrap system \"$PLIST\" 2>/dev/null || true\n";
    stream << "/bin/launchctl enable system/$LABEL\n";
    stream << "/bin/launchctl kickstart -k system/$LABEL\n";
    stream.flush();
    return content;
}

bool writeInstallScript(QTemporaryFile &file, const QString &daemonSource, const QString &libSource,
                        const QString &plistSource)
{
    const QString content = installScriptContent(daemonSource, libSource, plistSource);
    if (!file.open())
        return false;
    file.write(content.toUtf8());
    file.close();
    return file.error() == QFileDevice::NoError;
}

// 通过 osascript 弹出系统管理员授权框执行脚本（无需代码签名）
bool runWithAdminPrivileges(const QString &scriptPath)
{
    const QString shellCommand = QStringLiteral("/bin/sh %1").arg(shellQuote(scriptPath));
    const QString appleScript = QStringLiteral("do shell script \"%1\" with administrator privileges").arg(shellCommand);
    return runProcess(QStringLiteral("/usr/bin/osascript"), {QStringLiteral("-e"), appleScript}, 120000);
}

// 生成 plist 与安装脚本，经管理员授权执行：
// 安装 helper 到 /Library/PrivilegedHelperTools + 写 plist + bootstrap 启动
bool installAndStartLaunchDaemon()
{
    const QString daemonSource = macDaemonBinaryPath();
    const QString libSource = macDaemonLibPath();
    const QString helperPath = QLatin1String(kMacHelperInstallPath);

    QTemporaryFile plistFile(QDir::tempPath() + QStringLiteral("/qtet-daemon-plist-XXXXXX.plist"));
    plistFile.setAutoRemove(true);
    if (!writeTemporaryPlist(plistFile, helperPath)) {
        LogHelper::logWarning(QStringLiteral("生成 qtet-daemon LaunchDaemon plist 临时文件失败"), "DaemonRegister");
        return false;
    }

    QTemporaryFile scriptFile(QDir::tempPath() + QStringLiteral("/qtet-daemon-install-XXXXXX.sh"));
    scriptFile.setAutoRemove(true);
    if (!writeInstallScript(scriptFile, daemonSource, libSource, plistFile.fileName())) {
        LogHelper::logWarning(QStringLiteral("生成 qtet-daemon 安装脚本临时文件失败"), "DaemonRegister");
        return false;
    }

    return runWithAdminPrivileges(scriptFile.fileName());
}

bool startLaunchDaemon()
{
    // 仅 kickstart 不够：若该 label 处于 disabled（卸载流程遗留的持久覆盖）或未被 launchd
    // 加载（例如上次安装 bootstrap 失败只留下 plist），kickstart 会直接失败。先 enable 清除
    // disabled 覆盖，再尝试 bootstrap（已加载时返回非零，忽略），最后以 kickstart 为准。
    const QString shellCommand =
        QStringLiteral("/bin/launchctl enable system/%1 2>/dev/null || true; "
                       "/bin/launchctl bootstrap system %2 2>/dev/null || true; "
                       "/bin/launchctl kickstart -k system/%1")
            .arg(QLatin1String(kMacLaunchDaemonLabel), shellQuote(QLatin1String(kMacLaunchDaemonPlistPath)));
    const QString appleScript = QStringLiteral("do shell script \"%1\" with administrator privileges").arg(shellCommand);
    return runProcess(QStringLiteral("/usr/bin/osascript"), {QStringLiteral("-e"), appleScript}, 120000);
}

bool uninstallLaunchDaemon()
{
    const QString command = QStringLiteral("/bin/launchctl bootout system/%1 2>/dev/null; /bin/launchctl disable system/%1 2>/dev/null; /bin/rm -f %2 %3 %4")
                                .arg(QLatin1String(kMacLaunchDaemonLabel),
                                     QLatin1String(kMacLaunchDaemonPlistPath),
                                     QLatin1String(kMacHelperInstallPath),
                                     QLatin1String(kMacFfiLibInstallPath));
    const QString appleScript = QStringLiteral("do shell script \"%1\" with administrator privileges").arg(command);
    return runProcess(QStringLiteral("/usr/bin/osascript"), {QStringLiteral("-e"), appleScript}, 120000);
}
#endif // macOS
} // namespace

DaemonRegisterHelper::RequiredAction DaemonRegisterHelper::requiredAction()
{
#if defined(Q_OS_LINUX) || defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    const QString path = daemonBinaryPath();
    const QFileInfo daemonInfo(path);

    if (!daemonInfo.exists() || !daemonInfo.isFile() || !daemonInfo.isExecutable()) {
        LogHelper::logWarning(QStringLiteral("daemon 二进制缺失或不可执行: exists=%1 file=%2 executable=%3 path=%4")
                                  .arg(daemonInfo.exists())
                                  .arg(daemonInfo.isFile())
                                  .arg(daemonInfo.isExecutable())
                                  .arg(path),
                              "DaemonRegister");
        LogHelper::logInfo(QStringLiteral("检测结果: daemon 二进制缺失，服务注册状态与进程状态未继续检测"), "DaemonRegister");
        return RequiredAction::DaemonBinaryMissing;
    }

#if defined(Q_OS_MACOS)
    // daemon 经 @loader_path 加载 EasyTier FFI（@rpath/libeasytier_ffi.dylib）。
    // bundle 内缺该库时，即便提权安装成功、launchd 也会因 dyld 加载失败而反复崩溃
    // （KeepAlive），故提前判定为二进制缺失，给出明确报错而非"装完连不上"。
    const QString libPath = macDaemonLibPath();
    const QFileInfo libInfo(libPath);
    if (!libInfo.exists() || !libInfo.isFile()) {
        LogHelper::logWarning(QStringLiteral("EasyTier FFI 动态库缺失: %1").arg(libPath), "DaemonRegister");
        return RequiredAction::DaemonBinaryMissing;
    }
#endif

    const bool registered = isServiceRegistered();
    if (!registered) {
        LogHelper::logInfo(QStringLiteral("检测结果: daemon 二进制可用，后端服务未注册，进程状态未继续检测"), "DaemonRegister");
        return RequiredAction::RegisterService;
    }

    const bool running = isDaemonProcessRunning();
    if (!running) {
        LogHelper::logInfo(QStringLiteral("检测结果: daemon 二进制可用，后端服务已注册，qtet-daemon 进程未运行"), "DaemonRegister");
        return RequiredAction::StartService;
    }

    LogHelper::logInfo(QStringLiteral("检测结果: daemon 二进制可用，后端服务已注册，qtet-daemon 进程运行中"), "DaemonRegister");
    return RequiredAction::None;
#else
    LogHelper::logInfo(QStringLiteral("检测结果: 当前平台不支持自动注册后端服务"), "DaemonRegister");
    return RequiredAction::UnsupportedPlatform;
#endif
}

DaemonRegisterHelper::EnsureResult DaemonRegisterHelper::ensureDaemonService()
{
#if defined(Q_OS_LINUX) || defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    const QString daemonPath = daemonBinaryPath();
    switch (requiredAction()) {
    case RequiredAction::None:
        return EnsureResult::AlreadyRunning;
    case RequiredAction::DaemonBinaryMissing:
        LogHelper::logWarning(QStringLiteral("qtet-daemon 不存在或不可执行：%1").arg(daemonPath), "DaemonRegister");
        return EnsureResult::DaemonBinaryMissing;
    case RequiredAction::RegisterService:
#if defined(Q_OS_LINUX)
        if (!installAndStartService(daemonPath)) {
            LogHelper::logWarning(QStringLiteral("注册并启动 qtet-daemon systemd 服务失败"), "DaemonRegister");
            return EnsureResult::RegisterFailed;
        }
        LogHelper::logInfo(QStringLiteral("已请求注册并启动 qtet-daemon systemd 服务"), "DaemonRegister");
#elif defined(Q_OS_MACOS)
        if (!installAndStartLaunchDaemon()) {
            LogHelper::logWarning(QStringLiteral("安装并启动 qtet-daemon LaunchDaemon 服务失败"), "DaemonRegister");
            return EnsureResult::RegisterFailed;
        }
        LogHelper::logInfo(QStringLiteral("已请求安装并启动 qtet-daemon LaunchDaemon 服务"), "DaemonRegister");
#else
        if (!installAndStartWindowsService()) {
            LogHelper::logWarning(QStringLiteral("注册并启动 qtet-daemon Windows 服务失败"), "DaemonRegister");
            return EnsureResult::RegisterFailed;
        }
        LogHelper::logInfo(QStringLiteral("已请求注册并启动 qtet-daemon Windows 服务"), "DaemonRegister");
#endif
        return EnsureResult::RegisteredAndStartRequested;
    case RequiredAction::StartService:
#if defined(Q_OS_LINUX)
        if (!startService()) {
            LogHelper::logWarning(QStringLiteral("启动 qtet-daemon systemd 服务失败"), "DaemonRegister");
            return EnsureResult::StartFailed;
        }
        LogHelper::logInfo(QStringLiteral("已请求启动 qtet-daemon systemd 服务"), "DaemonRegister");
#elif defined(Q_OS_MACOS)
        if (!startLaunchDaemon()) {
            LogHelper::logWarning(QStringLiteral("启动 qtet-daemon LaunchDaemon 服务失败"), "DaemonRegister");
            return EnsureResult::StartFailed;
        }
        LogHelper::logInfo(QStringLiteral("已请求启动 qtet-daemon LaunchDaemon 服务"), "DaemonRegister");
#else
        if (!startWindowsService()) {
            LogHelper::logWarning(QStringLiteral("启动 qtet-daemon Windows 服务失败"), "DaemonRegister");
            return EnsureResult::StartFailed;
        }
        LogHelper::logInfo(QStringLiteral("已请求启动 qtet-daemon Windows 服务"), "DaemonRegister");
#endif
        return EnsureResult::StartRequested;
    case RequiredAction::UnsupportedPlatform:
        return EnsureResult::UnsupportedPlatform;
    }
    return EnsureResult::UnsupportedPlatform;
#else
    return EnsureResult::UnsupportedPlatform;
#endif
}

bool DaemonRegisterHelper::uninstallDaemonService()
{
#if defined(Q_OS_LINUX) || defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    if (!isServiceRegistered()) {
        LogHelper::logWarning(QStringLiteral("卸载忽略: 后端服务尚未注册"), "DaemonRegister");
        return false;
    }
#if defined(Q_OS_LINUX)
    if (!uninstallService()) {
        LogHelper::logWarning(QStringLiteral("停止并卸载 qtet-daemon systemd 服务失败"), "DaemonRegister");
        return false;
    }
    LogHelper::logInfo(QStringLiteral("已请求停止并卸载 qtet-daemon systemd 服务"), "DaemonRegister");
#elif defined(Q_OS_MACOS)
    if (!uninstallLaunchDaemon()) {
        LogHelper::logWarning(QStringLiteral("停止并卸载 qtet-daemon LaunchDaemon 服务失败"), "DaemonRegister");
        return false;
    }
    LogHelper::logInfo(QStringLiteral("已请求停止并卸载 qtet-daemon LaunchDaemon 服务"), "DaemonRegister");
#else
    if (!uninstallWindowsService()) {
        LogHelper::logWarning(QStringLiteral("停止并卸载 qtet-daemon Windows 服务失败"), "DaemonRegister");
        return false;
    }
    LogHelper::logInfo(QStringLiteral("已请求停止并卸载 qtet-daemon Windows 服务"), "DaemonRegister");
#endif
    return true;
#else
    LogHelper::logInfo(QStringLiteral("当前平台不支持卸载后端服务"), "DaemonRegister");
    return false;
#endif
}

bool DaemonRegisterHelper::isServiceRegistered()
{
#if defined(Q_OS_LINUX)
    if (g_serviceRegisteredOverrideEnabled)
        return g_serviceRegisteredOverride;
    return QFile::exists(systemdServicePath());
#elif defined(Q_OS_WIN)
    if (g_serviceRegisteredOverrideEnabled)
        return g_serviceRegisteredOverride;
    QString output;
    return runProcessWithOutput(QStringLiteral("sc"), {QStringLiteral("query"), QLatin1String(kWindowsServiceName)}, &output, 5000);
#elif defined(Q_OS_MACOS)
    if (g_serviceRegisteredOverrideEnabled)
        return g_serviceRegisteredOverride;
    return QFile::exists(macPlistPath());
#else
    return false;
#endif
}

bool DaemonRegisterHelper::isDaemonProcessRunning()
{
#if defined(Q_OS_LINUX)
    if (g_processRunningOverrideEnabled)
        return g_processRunningOverride;
    return runProcess(QStringLiteral("pidof"), {QLatin1String(kDaemonName)}, 5000);
#elif defined(Q_OS_WIN)
    if (g_processRunningOverrideEnabled)
        return g_processRunningOverride;
    QString output;
    if (!runProcessWithOutput(QStringLiteral("sc"), {QStringLiteral("query"), QLatin1String(kWindowsServiceName)}, &output, 5000))
        return false;
    return output.contains(QStringLiteral("RUNNING"), Qt::CaseInsensitive);
#elif defined(Q_OS_MACOS)
    if (g_processRunningOverrideEnabled)
        return g_processRunningOverride;
    // 注意不能用 `pgrep -x qtet-daemon`：helper 安装名为 cn.qtet.qteasytier.daemon
    // （非 qtet-daemon），进程名匹配永远失败 → 会被误判为"已注册但未运行"，前端因而
    // 反复 kickstart 且始终显示未安装。改用 launchctl 查询 launchd 中该服务的真实状态：
    // 退出码为 0 表示已加载，输出含 "state = running" 才表示进程正在运行。
    QString output;
    const QString target = QStringLiteral("system/") + QLatin1String(kMacLaunchDaemonLabel);
    if (!runProcessWithOutput(QStringLiteral("/bin/launchctl"),
                              {QStringLiteral("print"), target}, &output, 5000))
        return false;
    return output.contains(QStringLiteral("state = running"));
#else
    return false;
#endif
}

QString DaemonRegisterHelper::daemonBinaryPath()
{
#if defined(Q_OS_LINUX)
    return linuxDaemonBinaryPath();
#elif defined(Q_OS_WIN)
    return windowsDaemonBinaryPath();
#elif defined(Q_OS_MACOS)
    return macDaemonBinaryPath();
#else
    return QString();
#endif
}

#if defined(Q_OS_LINUX)
void DaemonRegisterHelper::setSystemdServicePathOverrideForTesting(const QString &path)
{
    g_servicePathOverride = path;
}

void DaemonRegisterHelper::setDaemonBinaryPathOverrideForTesting(const QString &path)
{
    g_daemonPathOverride = path;
}

void DaemonRegisterHelper::setDaemonProcessRunningOverrideForTesting(bool enabled, bool running)
{
    g_processRunningOverrideEnabled = enabled;
    g_processRunningOverride = running;
}

QString DaemonRegisterHelper::systemdServiceContentForTesting(const QString &daemonPath)
{
    return serviceContent(daemonPath);
}
#endif

#if defined(Q_OS_WIN)
void DaemonRegisterHelper::setDaemonBinaryPathOverrideForTesting(const QString &path)
{
    g_daemonPathOverride = path;
}

void DaemonRegisterHelper::setServiceRegisteredOverrideForTesting(bool enabled, bool registered)
{
    g_serviceRegisteredOverrideEnabled = enabled;
    g_serviceRegisteredOverride = registered;
}

void DaemonRegisterHelper::setDaemonProcessRunningOverrideForTesting(bool enabled, bool running)
{
    g_processRunningOverrideEnabled = enabled;
    g_processRunningOverride = running;
}
#endif

#if defined(Q_OS_MACOS)
void DaemonRegisterHelper::setLaunchDaemonPlistPathOverrideForTesting(const QString &path)
{
    g_servicePathOverride = path;
}

void DaemonRegisterHelper::setDaemonBinaryPathOverrideForTesting(const QString &path)
{
    g_daemonPathOverride = path;
}

void DaemonRegisterHelper::setServiceRegisteredOverrideForTesting(bool enabled, bool registered)
{
    g_serviceRegisteredOverrideEnabled = enabled;
    g_serviceRegisteredOverride = registered;
}

void DaemonRegisterHelper::setDaemonProcessRunningOverrideForTesting(bool enabled, bool running)
{
    g_processRunningOverrideEnabled = enabled;
    g_processRunningOverride = running;
}

QString DaemonRegisterHelper::launchDaemonPlistContentForTesting(const QString &daemonPath)
{
    return launchDaemonPlistContent(daemonPath);
}

QString DaemonRegisterHelper::installScriptContentForTesting(const QString &daemonPath, const QString &libPath,
                                                             const QString &plistPath)
{
    return installScriptContent(daemonPath, libPath, plistPath);
}
#endif
