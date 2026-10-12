#!/usr/bin/env bash
#
# QtEasyTier macOS 打包脚本。
#
# 输入：CMake 构建产物 build/Output/appQtEasyTier.app（由根 CMakeLists.txt 生成，
#       macOS 下可为 Universal 或单架构，且已把 qtet-daemon 收进 Contents/MacOS）。
# 处理：macdeployqt 部署 Qt 框架与 QML 依赖 → ad-hoc 代码签名 → 打包 zip/dmg
#       （zip/dmg 内应用名为 QtEasyTier.app，dmg 附 /Applications 便于拖拽安装）。
#
# 用法：cd build/Output && bash ../../package/macos/build_macos.sh -v 3.3.0 [-q <Qt>/bin]
#
# 说明：无 Developer ID，采用 ad-hoc 签名（codesign -s -）。产物未公证，
#       用户首次打开需「右键 → 打开」，或执行
#       `xattr -dr com.apple.quarantine /Applications/QtEasyTier.app`。
set -euo pipefail

VERSION=""
QT_BIN_DIR=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        -v|--version)
            if [[ $# -lt 2 ]]; then
                echo "错误: $1 需要一个参数"
                exit 1
            fi
            VERSION="$2"
            shift 2
            ;;
        -q|--qt-bin)
            if [[ $# -lt 2 ]]; then
                echo "错误: $1 需要一个参数（构建所用 Qt 的 bin 目录）"
                exit 1
            fi
            QT_BIN_DIR="$2"
            shift 2
            ;;
        *)
            echo "未知参数: $1"
            exit 1
            ;;
    esac
done

if [[ -z "$VERSION" ]]; then
    echo "用法: $(basename "$0") -v <版本号> [-q <Qt>/bin]"
    echo "示例: $(basename "$0") -v 3.3.0 -q ~/Qt/6.12.0/macos/bin"
    echo "  -q 用于指定与构建一致的 Qt 的 bin 目录（内含 macdeployqt）；"
    echo "     缺省时自动探测，务必避免误用其它大版本 Qt 的 macdeployqt。"
    exit 1
fi

OUTPUT_DIR="$(pwd)"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
QML_DIR="$SRC_ROOT/src/qml"

APP_NAME="appQtEasyTier"
APP_PATH="$OUTPUT_DIR/${APP_NAME}.app"

if [[ ! -d "$APP_PATH" ]]; then
    echo "错误: 未找到应用包: $APP_PATH" >&2
    echo "请先完成构建（cmake --build build），产物应位于 build/Output" >&2
    exit 1
fi

# macOS 应用运行依赖包内同目录的 qtet-daemon 与 EasyTier FFI 动态库
# （前端首次连接时经管理员授权安装到 /Library/PrivilegedHelperTools）。
for f in qtet-daemon libeasytier_ffi.dylib; do
    if [[ ! -f "$APP_PATH/Contents/MacOS/$f" ]]; then
        echo "错误: 应用包缺少 Contents/MacOS/$f" >&2
        echo "请以 BUILD_WITH_DAEMON=ON 构建（默认），否则发布的 .app 无法工作" >&2
        exit 1
    fi
done

# 架构标签由主可执行文件实际架构推导，避免单架构本地构建被误标为 universal
ARCHS="$(lipo -archs "$APP_PATH/Contents/MacOS/$APP_NAME" 2>/dev/null || echo unknown)"
if [[ "$ARCHS" == *"arm64"* && "$ARCHS" == *"x86_64"* ]]; then
    ARCH_TAG="universal"
elif [[ "$ARCHS" == *"arm64"* ]]; then
    ARCH_TAG="arm64"
elif [[ "$ARCHS" == *"x86_64"* ]]; then
    ARCH_TAG="x86_64"
else
    ARCH_TAG="$ARCHS"
fi

ZIP_NAME="qteasytier_v${VERSION}_macos_${ARCH_TAG}.zip"
DMG_NAME="qteasytier_v${VERSION}_macos_${ARCH_TAG}.dmg"

echo "[INFO] 输出目录: $OUTPUT_DIR"
echo "[INFO] 版本号: $VERSION"
echo "[INFO] 应用包: $APP_PATH"
echo "[INFO] 架构: $ARCHS -> $ARCH_TAG"

# ---------------------------------------------------------------------------
# 选择与构建所用 Qt「大版本一致」的 macdeployqt。
# PATH 上的 macdeployqt 可能来自 Homebrew qt@5：它会把 Qt5 框架部署进 Qt6 应用，
# 包内插件被改写为 Qt5 版本 → 运行时报
#   `Could not find the Qt platform plugin "cocoa"`。必须按大版本匹配后使用。
# 以「主可执行文件链接的 QtCore.framework current version 主号」判定应用 Qt 大版本。
# ---------------------------------------------------------------------------
qt_major_of() {
    otool -L "$1" 2>/dev/null \
        | grep -E 'QtCore\.framework' \
        | grep -oE 'current version [0-9]+' | head -1 | grep -oE '[0-9]+'
}

APP_QT_MAJOR="$(qt_major_of "$APP_PATH/Contents/MacOS/$APP_NAME")"
if [[ -z "$APP_QT_MAJOR" ]]; then
    echo "错误: 无法判定应用链接的 Qt 版本" >&2
    exit 1
fi

MACDEPLOYQT_BIN=""
MDQ_CANDIDATES=()
[[ -n "$QT_BIN_DIR" ]] && MDQ_CANDIDATES+=("$QT_BIN_DIR/macdeployqt")
[[ -n "${MACDEPLOYQT:-}" ]] && MDQ_CANDIDATES+=("$MACDEPLOYQT")
for c in "$HOME"/Qt/*/macos/bin/macdeployqt \
         /opt/homebrew/opt/qt/bin/macdeployqt \
         /opt/homebrew/opt/qt@6/bin/macdeployqt; do
    [[ -e "$c" ]] && MDQ_CANDIDATES+=("$c")
done
PATH_MDQ="$(command -v macdeployqt 2>/dev/null || true)"
[[ -n "$PATH_MDQ" ]] && MDQ_CANDIDATES+=("$PATH_MDQ")

for c in "${MDQ_CANDIDATES[@]:-}"; do
    [[ -x "$c" ]] || continue
    if [[ "$(qt_major_of "$c")" == "$APP_QT_MAJOR" ]]; then
        MACDEPLOYQT_BIN="$c"
        break
    fi
done

if [[ -z "$MACDEPLOYQT_BIN" ]]; then
    echo "错误: 未找到与 Qt $APP_QT_MAJOR 匹配的 macdeployqt" >&2
    echo "候选及其 Qt 大版本：" >&2
    for c in "${MDQ_CANDIDATES[@]:-}"; do
        [[ -x "$c" ]] && echo "  $c -> Qt $(qt_major_of "$c")" >&2
    done
    echo "请用 -q <构建所用 Qt>/bin 显式指定。" >&2
    exit 1
fi
echo "[INFO] macdeployqt: $MACDEPLOYQT_BIN (Qt $APP_QT_MAJOR)"

# 1. 部署 Qt 框架、插件与 QML 依赖到 .app 内部
echo "[INFO] macdeployqt 部署 Qt 运行库..."
"$MACDEPLOYQT_BIN" "$APP_PATH" -qmldir="$QML_DIR" -always-overwrite

# 部署后校验：平台插件确实链接同大版本 Qt（防止再次误用其它 Qt 的 macdeployqt）
PLUGIN_QT_MAJOR="$(qt_major_of "$APP_PATH/Contents/PlugIns/platforms/libqcocoa.dylib")"
if [[ "$PLUGIN_QT_MAJOR" != "$APP_QT_MAJOR" ]]; then
    echo "错误: 部署后 cocoa 平台插件链接 Qt $PLUGIN_QT_MAJOR，与应用的 Qt $APP_QT_MAJOR 不一致" >&2
    echo "macdeployqt 选择有误，请用 -q 指定正确 Qt 的 bin 目录后重试。" >&2
    exit 1
fi

# 1b. 清掉用不到的 SQL 驱动（本应用仅用 QSQLITE）。ODBC/Mimer/PostgreSQL 驱动在部分
#     环境会引用缺失的系统库（libiodbc / libpq / libmimerapi），部署时刷错误、留在产物里
#     也只是不可加载的无用二进制；仅保留 libqsqlite。须在签名之前删除。
if [[ -d "$APP_PATH/Contents/PlugIns/sqldrivers" ]]; then
    find "$APP_PATH/Contents/PlugIns/sqldrivers" -name 'libqsql*.dylib' \
        ! -name 'libqsqlite.dylib' -delete
fi

# 2. ad-hoc 代码签名（无 Developer ID）。先签内嵌的 daemon/FFI，再签整个 bundle
#    （--deep 覆盖 Qt 框架与插件）。注意：签名必须在 macdeployqt 之后，否则框架被
#    修改会导致签名失效。
echo "[INFO] ad-hoc 代码签名..."
codesign --force --sign - "$APP_PATH/Contents/MacOS/qtet-daemon"
codesign --force --sign - "$APP_PATH/Contents/MacOS/libeasytier_ffi.dylib"
codesign --force --deep --sign - "$APP_PATH"
codesign --verify --deep --strict --verbose=2 "$APP_PATH" || true

# 3. 组装发布用 bundle：把应用目录改名为产品名 QtEasyTier.app（与 Info.plist 的
#    CFBundleName 一致，Finder 里显示得体），dmg 内附 /Applications 软链接以便拖拽安装。
#    ditto 保留签名、符号链接与权限。
STAGE_ROOT="$(mktemp -d)"   # 置于 $TMPDIR，避开 Finder 在项目目录里注入 .DS_Store
trap 'rm -rf "$STAGE_ROOT"' EXIT
mkdir -p "$STAGE_ROOT/dmg"
ditto "$APP_PATH" "$STAGE_ROOT/dmg/QtEasyTier.app"
ln -s /Applications "$STAGE_ROOT/dmg/Applications"
# 清掉可能混入的 .DS_Store，保持发布包整洁
find "$STAGE_ROOT/dmg" -name .DS_Store -delete
# 发布前的硬校验：改名前后的 bundle 签名均须有效（部署正确时该检查会通过）
codesign --verify --verbose=1 "$STAGE_ROOT/dmg/QtEasyTier.app"

# 4. 打包 zip（--keepParent 让压缩包内顶层即为 QtEasyTier.app）
echo "[INFO] 生成 zip..."
rm -f "$OUTPUT_DIR/$ZIP_NAME"
ditto -c -k --sequesterRsrc --keepParent "$STAGE_ROOT/dmg/QtEasyTier.app" "$OUTPUT_DIR/$ZIP_NAME"

# 5. 生成 dmg（压缩只读，含 /Applications 拖拽目标）
if command -v hdiutil >/dev/null 2>&1; then
    echo "[INFO] 生成 dmg..."
    find "$STAGE_ROOT/dmg" -name .DS_Store -delete
    rm -f "$OUTPUT_DIR/$DMG_NAME"
    hdiutil create -volname "QtEasyTier" \
        -srcfolder "$STAGE_ROOT/dmg" \
        -ov -format UDZO "$OUTPUT_DIR/$DMG_NAME"
fi

echo "[INFO] 打包完成:"
echo "  $OUTPUT_DIR/$ZIP_NAME"
if [[ -f "$OUTPUT_DIR/$DMG_NAME" ]]; then
    echo "  $OUTPUT_DIR/$DMG_NAME"
fi
