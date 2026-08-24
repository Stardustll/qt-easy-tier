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
    // 实例化即应用系统主题（默认跟随亮/暗），无需调用版本敏感的 palette API
    DGuiApplicationHelper::instance();
}

#else

void DtkIntegration::initEarly() {}
void DtkIntegration::initAfterApp() {}

#endif
