/**
 * @file DtkIntegration.cpp
 * @brief DtkIntegration 实现
 */
#include "DtkIntegration.h"

#ifdef QTET_ENABLE_DTK

#include <DGuiApplicationHelper>
#include <QQuickStyle>

void DtkIntegration::initEarly()
{
    // 必须在第一个 Qt Quick Controls 控件创建前设置（dtk-development chameleon-qml 约束）
    QQuickStyle::setStyle(QStringLiteral("Chameleon"));
}

void DtkIntegration::initAfterApp()
{
    auto *helper = DGuiApplicationHelper::instance();
    // 显式跟随系统主题；若 API 名在目标 dtkgui 版本中不存在，仅实例化 helper 即可（默认即跟随系统）
    if (helper && helper->applicationPaletteType() == DGuiApplicationHelper::UnknownType)
        helper->setApplicationPaletteType(DGuiApplicationHelper::SystemType);
}

#else

void DtkIntegration::initEarly() {}
void DtkIntegration::initAfterApp() {}

#endif
