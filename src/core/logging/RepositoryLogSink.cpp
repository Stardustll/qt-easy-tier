/** @file RepositoryLogSink.cpp @brief RepositoryLogSink 实现 */
#include "RepositoryLogSink.h"

#include "log/LogDispatcher.h"
#include "sqlite_repository/LogRepository.h"

RepositoryLogSink::RepositoryLogSink(LogRepository *repository, QObject *parent)
    : QObject(parent)
    , m_repository(repository)
{
}

RepositoryLogSink::~RepositoryLogSink()
{
    // 与 SystemTrayManager 对 TrayMessageDispatcher 的处理一致：分发器只持有裸指针，
    // 析构不注销会让下一次日志分发写入已释放的 sink
    LogDispatcher::instance()->removeSink(this);
}

void RepositoryLogSink::write(const LogEntry &entry)
{
    // 仓库不可用时静默丢弃日志
    if (!m_repository)
        return;
    // 插入日志并自动清理超出 maxEntries 的旧记录
    m_repository->insertLog(entry.level, entry.message, entry.context, m_maxEntries);
}

int RepositoryLogSink::maxEntries() const
{
    return m_maxEntries;
}

void RepositoryLogSink::setMaxEntries(int maxEntries)
{
    m_maxEntries = maxEntries;
}
