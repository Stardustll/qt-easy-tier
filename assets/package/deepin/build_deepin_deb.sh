#!/usr/bin/env bash
# deepin v25 DTK 专用 deb 打包脚本：复用通用 linux 打包逻辑，仅调整包名与 DEBIAN 目录
set -euo pipefail

VERSION=""
ARCH="amd64"

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
        -a|--arch)
            if [[ $# -lt 2 ]]; then
                echo "错误: $1 需要一个参数"
                exit 1
            fi
            ARCH="$2"
            shift 2
            ;;
        *)
            echo "未知参数: $1"
            exit 1
            ;;
    esac
done

if [[ -z "$VERSION" ]]; then
    echo "用法: $(basename "$0") -v <版本号> [-a <架构>]"
    exit 1
fi

case "$ARCH" in
    amd64|arm64) ;;
    *) echo "错误: 不支持的架构: $ARCH"; exit 1 ;;
esac

# 从 qmake 实际输出目录读取构建产物（兼容 build/ 与 build-release/ 等目录）
if [[ -n "${QTET_OUTPUT_DIR:-}" ]]; then
    OUTPUT_DIR="$QTET_OUTPUT_DIR"
else
    OUTPUT_DIR="$(pwd)/build/Output"
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ASSETS_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

PACKAGE_NAME="qteasytier-deepin"
BUILD_DIR="$(pwd)/${PACKAGE_NAME}_${ARCH}"
DEB_NAME="${PACKAGE_NAME}_v${VERSION}_linux_${ARCH}.deb"

echo "[INFO] 输出目录: $OUTPUT_DIR"
echo "[INFO] 包名: $DEB_NAME"

if [[ ! -d "$OUTPUT_DIR" ]] || [[ -z "$(ls -A "$OUTPUT_DIR" 2>/dev/null)" ]]; then
    echo "错误: 构建产物目录 $OUTPUT_DIR 不存在或为空，请先构建（需 -DQTET_ENABLE_DTK=ON）"
    exit 1
fi

rm -rf "$BUILD_DIR"

mkdir -p "$BUILD_DIR/DEBIAN"
mkdir -p "$BUILD_DIR/opt/qteasytier"
mkdir -p "$BUILD_DIR/usr/share/applications"
mkdir -p "$BUILD_DIR/etc/systemd/system"

echo "[INFO] 复制程序文件..."
for f in "$OUTPUT_DIR"/*; do
    if [ -f "$f" ]; then
        basename_f=$(basename "$f")
        if [[ "$basename_f" == *.a ]] || [[ "$basename_f" == tst* ]] || [[ "$basename_f" == *.AppImage ]]; then
            echo "[INFO] 跳过: $basename_f"
            continue
        fi
        cp -a "$f" "$BUILD_DIR/opt/qteasytier/"
    fi
done

echo "[INFO] 复制图标..."
cp -a "$ASSETS_DIR/favicon/qtet.png" "$BUILD_DIR/opt/qteasytier/"

echo "[INFO] 复制控制文件..."
sed \
    -e "s/^Version: .*/Version: ${VERSION}/" \
    -e "s/^Architecture: .*/Architecture: ${ARCH}/" \
    "$SCRIPT_DIR/DEBIAN/control" > "$BUILD_DIR/DEBIAN/control"
cp -a "$SCRIPT_DIR/DEBIAN/postinst" "$BUILD_DIR/DEBIAN/postinst"
cp -a "$SCRIPT_DIR/DEBIAN/prerm" "$BUILD_DIR/DEBIAN/prerm"
cp -a "$SCRIPT_DIR/qteasytier.desktop" "$BUILD_DIR/usr/share/applications/"
cp -a "$SCRIPT_DIR/qtet-daemon.service" "$BUILD_DIR/etc/systemd/system/"

chmod 755 "$BUILD_DIR/DEBIAN/postinst"
chmod 755 "$BUILD_DIR/DEBIAN/prerm"
chmod 644 "$BUILD_DIR/DEBIAN/control"

find "$BUILD_DIR/opt/qteasytier" -type f -exec chmod 755 {} +
find "$BUILD_DIR/opt/qteasytier" -type d -exec chmod 755 {} +
chmod 644 "$BUILD_DIR/usr/share/applications/qteasytier.desktop"
chmod 644 "$BUILD_DIR/etc/systemd/system/qtet-daemon.service"

echo "[INFO] 构建 Debian 包..."
dpkg-deb --build "$BUILD_DIR" "$(pwd)/$DEB_NAME"

rm -rf "$BUILD_DIR"

echo "[INFO] 打包完成: $(pwd)/$DEB_NAME"
