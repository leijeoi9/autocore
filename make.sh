#!/bin/bash
# ============================================================
# make.sh — AutoCore 一键构建脚本
# 用法:
#   ./make.sh         本地编译 (x86)
#   ./make.sh arm     交叉编译 (ARM / I.MX6ull)
# ============================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

if [ "$1" = "arm" ]; then
    echo "=== Cross-compiling for ARM (I.MX6ull) ==="
    BUILD_DIR="${SCRIPT_DIR}/build-arm"
    TOOLCHAIN="${SCRIPT_DIR}/cmake/arm-linux-gnueabihf.cmake"
    mkdir -p "${BUILD_DIR}"
    cd "${BUILD_DIR}"
    cmake -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN}" ..
    make -j$(nproc)
    echo ""
    echo "ARM build complete: ${BUILD_DIR}/app/can_service/can_service"
    file "${BUILD_DIR}/app/can_service/can_service"
else
    echo "=== Building for local (x86) ==="
    BUILD_DIR="${SCRIPT_DIR}/build"
    mkdir -p "${BUILD_DIR}"
    cd "${BUILD_DIR}"
    cmake ..
    make -j$(nproc)
    echo ""
    echo "Build complete: ${BUILD_DIR}/app/can_service/can_service"
fi
