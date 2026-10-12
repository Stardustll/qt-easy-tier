/**
 * @file RepositoryLogSink.h
 * @brief 仓库日志槽，将日志条目写入 LogRepository，支持最大条数限制
 */
#pragma once

#include "log/LogSink.h"

#include <QObject>

class LogRepository;

/** @brief 将日志持久化到 LogRepository 的日志槽，可配置最大保留条数 */
class RepositoryLogSink final : public QObject, public LogSink
{
    Q_OBJECT

public:
    /** @brief 构造函数 @param repository 日志仓库 @param parent 父对象 */
    explicit RepositoryLogSink(LogRepository *repository, QObject *parent = nullptr);

    /**
     * @brief 析构时从全局 LogDispatcher 注销
     *
     * LogDispatcher 只持有 sink 裸指针（不做生命周期管理），析构必须主动注销，
     * 否则下一次写日志会分发到已释放的 sink 上：跨用例/跨生命周期重建 AppServices
     * 时必现（崩溃点在 sink 持有的 repository 上的 SQL 调用）。
     */
    ~RepositoryLogSink() override;

    /** @brief 写入一条日志条目到仓库 @param entry 日志条目 */
    void write(const LogEntry &entry) override;

    /** @brief 获取最大保留日志条数 @return 当前最大条数 */
    int maxEntries() const;
    /** @brief 设置最大保留日志条数 @param maxEntries 最大条数 */
    void setMaxEntries(int maxEntries);

private:
    LogRepository *m_repository = nullptr;
    int m_maxEntries = 100;
};
