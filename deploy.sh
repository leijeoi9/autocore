#!/bin/bash
# ============================================================
# deploy.sh — AutoCore 板端部署脚本
# 用途：将交叉编译产物部署到 I.MX6ull 开发板
# ============================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build-arm"

# I.MX6ull 板子信息
# 正点原子 I.MX6ull 默认 IP（根据实际修改）
BOARD_IP="${BOARD_IP:-192.168.1.100}"
BOARD_USER="${BOARD_USER:-root}"
BOARD_DIR="${BOARD_DIR:-/root/autocore}"

echo "=========================================="
echo " AutoCore — Deploy to I.MX6ull"
echo "=========================================="
echo "Board IP : ${BOARD_IP}"
echo "Target   : ${BOARD_DIR}"
echo "Build    : ${BUILD_DIR}"
echo ""

# 1. 检查交叉编译产物
if [ ! -f "${BUILD_DIR}/app/can_service/can_service" ]; then
    echo "[ERROR] ARM binary not found. Run 'make' in build-arm/ first."
    echo "  cd ${BUILD_DIR} && make -j\$(nproc)"
    exit 1
fi

echo "[1/3] Checking ARM binary..."
file "${BUILD_DIR}/app/can_service/can_service" | grep -q "ARM" || {
    echo "[ERROR] Binary is not ARM architecture!"
    file "${BUILD_DIR}/app/can_service/can_service"
    exit 1
}
echo "  OK (ARM executable)"

# 2. 在板子上创建目标目录
echo "[2/3] Creating directory on board..."
ssh "${BOARD_USER}@${BOARD_IP}" "mkdir -p ${BOARD_DIR}" 2>/dev/null || {
    echo "[WARN] SSH failed. Check BOARD_IP or connect manually."
    echo "  Files are at: ${BUILD_DIR}/app/can_service/can_service"
    echo "  Static libs:  ${BUILD_DIR}/*/lib*.a"
    exit 1
}

# 3. 复制文件
echo "[3/3] Copying files to board..."
scp "${BUILD_DIR}/app/can_service/can_service" "${BOARD_USER}@${BOARD_IP}:${BOARD_DIR}/"

echo ""
echo "=========================================="
echo " Deploy complete!"
echo "=========================================="
echo ""
echo "On the board, run:"
echo "  ssh ${BOARD_USER}@${BOARD_IP}"
echo "  cd ${BOARD_DIR}"
echo ""
echo "Before running can_service, configure CAN:"
echo "  # 加载 CAN 内核模块"
echo "  modprobe can"
echo "  modprobe can_raw"
echo "  modprobe flexcan"
echo ""
echo "  # 配置 CAN 接口（正点原子 I.MX6ull 默认 can0）"
echo "  ip link set can0 up type can bitrate 500000"
echo ""
echo "  # 运行"
echo "  ./can_service"
echo ""
echo "To test UDS via CAN (from another machine):"
echo "  cansend can0 7E0#1003            # 切换到扩展会话"
echo "  candump can0                     # 看响应"
