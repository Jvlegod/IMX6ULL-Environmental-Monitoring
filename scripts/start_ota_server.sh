#!/bin/bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK_ROOT="${QT_SDK_ROOT:-/home/jvle/Desktop/works/IMX6ULL/toolchains/qt5}"
SDK_ENV="${SDK_ROOT}/environment-setup-cortexa7hf-neon-poky-linux-gnueabi"
BUILD_DIR="${PROJECT_ROOT}/build-arm"
OTA_DIR="${PROJECT_ROOT}/ota"
PORT=18080
VERSION="$(date +%Y.%m.%d.%H%M%S)"

usage() {
    cat <<EOF
用法: $(basename "$0") [-v version] [-p port]

选项:
  -v version  OTA 版本号, 默认使用当前时间
  -p port     HTTP 端口, 默认 18080
  -h          显示帮助
EOF
}

while getopts ":v:p:h" option; do
    case "$option" in
        v) VERSION="$OPTARG" ;;
        p) PORT="$OPTARG" ;;
        h) usage; exit 0 ;;
        :) echo "缺少选项参数: -$OPTARG" >&2; usage >&2; exit 2 ;;
        \?) echo "未知选项: -$OPTARG" >&2; usage >&2; exit 2 ;;
    esac
done

if [ ! -f "$SDK_ENV" ]; then
    echo "找不到 Qt ARM 工具链环境文件: $SDK_ENV" >&2
    exit 1
fi

export CCACHE_PATH="${CCACHE_PATH:-}"
set +u
source "$SDK_ENV"
set -u
COMPILER_DIR="${OECORE_NATIVE_SYSROOT}/usr/bin/arm-poky-linux-gnueabi"
if [ ! -x "${COMPILER_DIR}/arm-poky-linux-gnueabi-g++" ]; then
    echo "找不到 ARM C++ 编译器: ${COMPILER_DIR}/arm-poky-linux-gnueabi-g++" >&2
    exit 1
fi

echo "[ota] 配置 ARM Qt 构建目录"
cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_SYSTEM_NAME=Linux \
    -DCMAKE_SYSTEM_PROCESSOR=arm \
    -DCMAKE_C_COMPILER="${COMPILER_DIR}/arm-poky-linux-gnueabi-gcc" \
    -DCMAKE_CXX_COMPILER="${COMPILER_DIR}/arm-poky-linux-gnueabi-g++" \
    -DCMAKE_C_FLAGS='-march=armv7ve -mfpu=neon -mfloat-abi=hard -mcpu=cortex-a7' \
    -DCMAKE_CXX_FLAGS='-march=armv7ve -mfpu=neon -mfloat-abi=hard -mcpu=cortex-a7' \
    -DCMAKE_EXE_LINKER_FLAGS='-Wl,-O1 -Wl,--hash-style=gnu -Wl,--as-needed' \
    -DCMAKE_SYSROOT="$SDKTARGETSYSROOT" \
    -DCMAKE_FIND_ROOT_PATH="$SDKTARGETSYSROOT" \
    -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
    -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
    -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
    -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY \
    -DCMAKE_PREFIX_PATH="$SDKTARGETSYSROOT/usr/lib/cmake" \
    -DOE_QMAKE_PATH_EXTERNAL_HOST_BINS="$OECORE_NATIVE_SYSROOT/usr/bin" \
    -DQt5_DIR="$SDKTARGETSYSROOT/usr/lib/cmake/Qt5"

echo "[ota] 编译 ARM Qt 程序"
cmake --build "$BUILD_DIR" -j"$(nproc)"

if ! file "$BUILD_DIR/environment_monitor" | grep -q 'ARM'; then
    echo "生成文件不是 ARM 程序: $BUILD_DIR/environment_monitor" >&2
    exit 1
fi

mkdir -p "$OTA_DIR"
cp -f "$BUILD_DIR/environment_monitor" "$OTA_DIR/environment_monitor"
python3 "$PROJECT_ROOT/scripts/make_ota_manifest.py" \
    --file "$OTA_DIR/environment_monitor" \
    --version "$VERSION" \
    --path /environment_monitor \
    --output "$OTA_DIR/manifest.json"

echo "[ota] OTA 文件已准备完成"
cat "$OTA_DIR/manifest.json"
echo
echo "[ota] 请在开发板 WiFi 配置中填写:"
echo "      OTA 服务器: $(hostname -I 2>/dev/null | awk '{print $1}')"
echo "      HTTP 端口: $PORT"
echo "      Manifest 路径: /manifest.json"
echo "[ota] 开发板连接 WiFi 后, 点击 检查并升级应用"
echo "[ota] 按 Ctrl+C 停止服务器"

cd "$OTA_DIR"
exec python3 -m http.server "$PORT" --bind 0.0.0.0
