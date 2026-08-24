/**
 * @file DtkIntegration.h
 * @brief deepin DTK 集成入口：Chameleon 风格与主题初始化
 *
 * 仅在 QTET_ENABLE_DTK 构建下生效；非 DTK 平台编译为空实现，
 * 不引入任何 dtkgui/dtkdeclarative 头文件依赖。
 */
#pragma once

namespace DtkIntegration {

/**
 * @brief 早期初始化：必须在创建任何 QML 引擎/控件之前调用
 *
 * 设置 Qt Quick Controls 2 风格为 Chameleon，使标准控件呈现 DTK 外观。
 */
void initEarly();

/**
 * @brief 应用初始化：在 QApplication 创建之后调用
 *
 * 实例化 DGuiApplicationHelper 并显式跟随系统主题（亮/暗），
 * 使 palette 颜色与 D.DTK.themeType 随系统自动切换。
 */
void initAfterApp();

} // namespace DtkIntegration
