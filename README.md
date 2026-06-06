# AutoCore — 车载智能网关

AUTOSAR 分层架构的车载智能网关，运行于 I.MX6ull (ARM Cortex-A7) 开发板。

## 架构

```
app/                    应用层 (Application SWC)
├── can_service/        CAN 通信 + UDS 诊断 主程序
├── uds_service/        UDS 诊断应用 (占位)
└── network_manage/     网络管理 (占位)

rte/                    RTE 运行时环境
├── rte.h               — 应用层统一接口
├── rte_vehicle_data.h  — 车辆数据结构
└── rte_com.h           — SWC 通信接口

bsw/                    基础软件层 (BSW)
├── mcal/can/           MCAL: CAN 驱动 (SocketCAN)
├── can/can_if/         CanIf: 路由/调度/信号解码
├── can/can_tp/         CanTp: ISO-TP 传输层 (占位)
├── diag/iso_tp/        ISO 15765-2 传输协议
├── diag/uds/           ISO 14229 诊断服务 (6 服务)
├── tcp_ip/socket_adapter/  Socket 适配层
├── tcp_ip/doip/        ISO 13400 DoIP 协议
├── sys/logger/         日志服务
├── sys/timer/          定时器 (占位)
├── sys/os/             OS 抽象 (占位)
└── mem/nvm/            NVRAM 管理 (占位)
```

## 实现的功能

### CAN 通信栈
- SocketCAN 底层驱动 (vcan0/can0)
- CAN ID 过滤、分发、回调注册
- 周期性报文调度
- CAN 信号解码 (engine_speed, vehicle_speed, engine_temp)

### ISO-TP (ISO 15765-2)
- 单帧 (SF) / 首帧 (FF) / 流控帧 (FC) / 连续帧 (CF)
- 发送/接收状态机
- 超时处理 (N_Ar/N_Bs/N_Cr)

### UDS 诊断 (ISO 14229)
| SID | 服务 | 说明 |
|-----|------|------|
| 0x10 | 诊断会话控制 | 默认/编程/扩展 会话切换 |
| 0x22 | 读取数据 (DID) | 发动机转速/车速/温度/VIN/版本 |
| 0x2E | 写入数据 (DID) | VIN 写入 (扩展/编程会话) |
| 0x27 | 安全访问 | 种子-密钥两步验证 |
| 0x19 | 读取故障码 | DTC 数量/列表报告 |
| 0x14 | 清除故障码 | 清空所有 DTC |

### DoIP (ISO 13400)
- UDP 车辆发现 (VIN 广播)
- TCP 诊断消息传输 (端口 13400)
- 路由激活

## 快速开始

```bash
# 本地编译 (x86)
./make.sh

# ARM 交叉编译 (I.MX6ull)
./make.sh arm

# 部署到开发板 (需要先设置 BOARD_IP)
BOARD_IP=192.168.1.100 ./deploy.sh
```

## 在 I.MX6ull 上运行

```bash
# 1. 加载 CAN 驱动
modprobe can
modprobe can_raw
modprobe flexcan

# 2. 配置 CAN
ip link set can0 up type can bitrate 500000

# 3. 运行网关
./can_service
```

## UDS 诊断测试

从另一台机器（或使用 can-utils）:

```bash
# 切换扩展会话
cansend can0 7E0#1003

# 读取发动机转速 (DID 0xF100)
cansend can0 7E0#22F100

# 请求种子
cansend can0 7E0#2701

# 发送密钥 (seed ^ 0x12345678)
cansend can0 7E0#2702[密钥]

# 读取故障码数量
cansend can0 7E0#1901
```

## 面试要点

这个项目展示了：
1. **AUTOSAR 分层架构** — 应用/RTE/BSW/MCAL 四层分离
2. **CAN 协议栈** — 从 SocketCAN 到信号解码
3. **ISO-TP 传输层** — 多帧传输状态机
4. **UDS 诊断** — 6 个核心服务
5. **DoIP** — 基于 IP 的诊断
6. **交叉编译** — x86 开发 → ARM 部署
7. **真实硬件** — 在 I.MX6ull 上运行验证
