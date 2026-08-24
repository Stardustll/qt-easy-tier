/* @brief 应用主窗口（deepin DTK 变体）：org.deepin.dtk 主窗口 + DTK 标题栏
 *
 * 仅用于 QTET_ENABLE_DTK 构建；经 CMake 资源别名以 qrc:/QtEasyTier/Main.qml
 * 暴露，main.cpp 加载路径不变。页面/组件文件完全复用，Chameleon 风格使
 * 标准 Qt Quick Controls 控件自动呈现 DTK 外观。
 *
 * import 别名说明：org.deepin.dtk 与 QtEasyTier 存在同名类型（Theme 等），
 * 故使用限定别名导入避免歧义。
 */
import org.deepin.dtk 1.0
import QtQuick
import QtQuick.Controls as QQC
import QtQuick.Layouts
import QtEasyTier as App

ApplicationWindow {
    id: root

    // 初始不显示，由 main.cpp 根据普通启动或 --autostart 决定是否展示窗口。
    visible: false
    width: 700
    height: 480
    minimumWidth: 500
    minimumHeight: 300
    title: qsTr("QtEasyTier")

    // 启用 DTK 窗口装饰（圆角/阴影；X11 下经 dxcb，Treeland 下经 personalization 协议）
    DWindow.enabled: true

    App.Theme { id: theme }

    // DTK 标题栏：自带窗口控制按钮与选项菜单（含 ThemeMenu 主题切换）
    header: TitleBar {
        title: qsTr("QtEasyTier")
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // 左侧导航侧边栏
            App.Sidebar {
                id: sidebar

                Layout.preferredWidth: 64
                Layout.fillHeight: true
                currentIndex: 0

                onItemClicked: function (index) {
                    sidebar.currentIndex = index;
                    pageContainer.currentIndex = index;
                }
            }

            // 右侧页面容器：封装页面切换动画
            App.PageContainer {
                id: pageContainer

                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: 0
            }
        }

        // 分隔线
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Qt.rgba(palette.windowText.r, palette.windowText.g, palette.windowText.b, 0.15)
        }

        // 底部状态栏：显示后端连接状态
        Rectangle {
            Layout.fillWidth: true
            height: 24
            color: palette.alternateBase

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 6

                // 状态指示灯
                Rectangle {
                    width: 8
                    height: 8
                    radius: 4
                    color: App.BackendStatusViewModel.connected ? theme.statusGreen
                         : App.BackendStatusViewModel.connecting ? theme.statusOrange
                         : theme.statusRed
                }

                QQC.Label {
                    text: App.BackendStatusViewModel.statusText
                    font.pixelSize: 11
                    color: palette.windowText
                }

                // 弹簧占位，把后续元素推到右侧
                Item { Layout.fillWidth: true }
            }
        }
    }

    // 全局错误弹窗：由 AppState 的 errorOccurred 信号驱动
    QQC.Dialog {
        id: errorDialog
        title: qsTr("错误")
        modal: true
        parent: QQC.Overlay.overlay
        anchors.centerIn: parent
        standardButtons: QQC.Dialog.Ok
        width: Math.min(420, parent ? parent.width - 48 : 360)

        property string text: ""

        QQC.Label {
            text: errorDialog.text
            wrapMode: Text.WordWrap
            width: parent ? parent.width : 360
        }
    }

    Connections {
        target: App.AppState
        function onErrorOccurred(message) {
            errorDialog.text = message
            errorDialog.open()
        }
    }
}
