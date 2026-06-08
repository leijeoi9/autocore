# CAN 收发器接线与调试指南

> 使用 SN65HVD230 模块 + 单块 I.MX6ull 板子自测

---

## 一、硬件接线

### SN65HVD230 模块引脚

```
SN65HVD230 模块              I.MX6ull
─────────────────────        ──────────────────────────
  VCC (3.3V)     ─────→     板子 3.3V 输出（底板排针上找 VCC_3V3）
  GND            ─────→     板子 GND（共地必须接）
  
  D (TXD)        ─────→     CAN1_TX（正点原子底板标 "CAN_TX" 或 "GPIO1_IO04"）
  R (RXD)        ─────→     CAN1_RX（正点原子底板标 "CAN_RX" 或 "GPIO1_IO05"）
  
  CANH           ─────→     接一个 120Ω 电阻到 CANL（回环测试用）
  CANL           ─────→     接一个 120Ω 电阻到 CANH
```

**如果你只有一块板子（自测模式）：**

```
SN65HVD230 CANH ──── 120Ω 电阻 ──── SN65HVD230 CANL
```

这样收发器自己的 TX 发出去，RX 收回来——形成回环。**不需要另一台设备。**

**如果你有两块板子或 USB-CAN：**

```
板子1 CANH ──── 120Ω ───┐
                         ├─ CANH 总线 ── 另一设备
板子1 CANL ─────────────┘
                         另一设备也需要 120Ω 终端电阻
```

### 正点原子底板引脚查找

大多数 I.MX6ull 底板在 **J8 或 CN5 座子**上有 CAN 引脚：

| 信号 | 底板标注 | I.MX6ull 引脚 |
|------|---------|--------------|
| CAN1_TX | CAN_TX 或 UART8_CTS | GPIO1_IO04 |
| CAN1_RX | CAN_RX 或 UART8_RTS | GPIO1_IO05 |
| 3.3V | VCC_3V3 或 3.3V | - |
| GND | GND | - |

**接线确认**：通电后，用万用表测 SN65HVD230 模块的 VCC 和 GND 之间是否有 3.3V。

---

## 二、板子上验证内核驱动

接好线后，**串口登录板子**（115200 波特率），执行：

```bash
# 1. 查看 flexcan 驱动是否存在
ls /lib/modules/$(uname -r)/kernel/drivers/net/can/
# 预期看到: flexcan.ko

# 2. 加载驱动
modprobe can
modprobe can_raw
modprobe flexcan

# 3. 确认加载成功
lsmod | grep can
# 预期看到: flexcan, can_raw, can

# 4. 配置 can0
ip link set can0 up type can bitrate 500000

# 5. 查看状态
ip -details link show can0
# 预期看到: mtu 16 ... state UP ... flexcan ... 
```

**常见问题：**

| 现象 | 原因 | 解决 |
|------|------|------|
| `modprobe flexcan` 报 `not found` | 内核没编译 flexcan 驱动 | 检查内核版本，可能需要重新编译内核 |
| `ip link set can0 up` 报 `No such device` | 设备树没使能 flexcan | 检查 /sys/firmware/devicetree，可能需要改设备树 |
| `ip link set can0 up` 报 `Operation not supported` | 内核没配置 CAN 子系统 | 重新编译内核打开 CAN 支持 |
| `ip link set can0 up` 报 `Cannot find net device` | 接口名不对 | 试 `can1` 或 `can2` |
| 驱动加载成功但 can0 起不来 | 可能需要先 `ip link set can0 down` 再 up | 先 down 再 up |

如果 **flexcan 不存在**，试试：

```bash
# 有些板子的内核是编译进内核而非模块
cat /sys/class/net/ | grep can
# 如果直接能看到 can0，说明驱动已内置

# 查看内核是否编译了 CAN 支持
cat /proc/version
# 或者查设备树
ls /sys/firmware/devicetree/base/soc/aips-bus/02000000.aips-bus/flexcan/
```

---

## 三、自测：回环模式测试

驱动加载成功后，先做**回环测试**（内部回环，总线不需要接另一台设备）：

```bash
# 1. 切换到回环模式
ip link set can0 down
ip link set can0 up type can bitrate 500000 loopback on

# 2. 验证 loopback 模式
ip -details link show can0
# 应该看到: loopback on

# 3. 回环收发自测
# 终端1（先开监听）
candump can0 &

# 终端2（发送）
cansend can0 123#AABBCCDDEEFF0011

# 如果终端1能收到自己发的报文 → 驱动和硬件都正常
# candump can0 输出: can0 123 [8] AA BB CC DD EE FF 00 11
```

**如果收不到**：
1. 确定 loopback 模式下收发器不需要 CANH/CANL 接线就能工作
2. 查 `ip -details link show can0` 看是否有错误计数
3. 查 `dmesg | grep flexcan` 看内核日志

---

## 四、板子运行 can_service

回环测试通过后，运行我们的网关程序：

### 方法 A：通过串口 base64 粘贴

```bash
# 1. 在 PC 上（已完成）
cd /home/leijeoi/autocore
base64 build-arm/app/can_service/can_service | xsel -b
# 或 cat 出来复制：
cat build-arm/app/can_service/can_service.b64
```

**在板子上**（串口终端）：

```bash
# 1. 创建一个目录
mkdir -p /root/autocore
cd /root/autocore

# 2. 粘贴 base64 内容（在 Windows 上从 certutil 输出复制 / Linux 从 cat 复制）
# 粘贴整个 base64 文本后：
cat > /tmp/can_service.b64 << 'ENDOFB64'
# 在这里粘贴全部 base64 文本...
ENDOFB64

# 如果有大量文本，用 MobaXterm 的粘贴功能直接粘贴到文件
# 然后解码
cat /tmp/can_service.b64 | base64 -d > /root/autocore/can_service
chmod +x /root/autocore/can_service
```

### 方法 B：U 盘传输

```bash
# PC 上复制到 U 盘
cp build-arm/app/can_service/can_service /media/USB/can_service

# 板子上挂载（FAT32 格式）
mount /dev/sda1 /mnt
cp /mnt/can_service /root/autocore/
chmod +x /root/autocore/can_service
```

### 运行

```bash
cd /root/autocore

# 确保 can0 已配置
ip link set can0 up type can bitrate 500000

# 运行网关（环境变量控制接口名）
CAN_IF=can0 ./can_service
```

**预期输出：**

```
===== AutoCore Gateway =====

[RTE] CAN interface: can0 (override with CAN_IF env var)
[RTE] CAN interface 'can0' opened (fd=3)
[RTE] RTE initialized successfully
[UDS] Initialized (req=0x7E0, resp=0x7E8)
[DoIP] Initialized: VIN=AUTOCORE123456789, Port=13400
[APP] DoIP listening on port 13400
[APP] All SWCs initialized, entering main loop...
```

注意区别上一次的输出——上次是：
```
[RTE] WARN: can_open(vcan0) failed — CAN disabled
```

这次应该是 **`CAN interface 'can0' opened (fd=3)`**，说明真 CAN 硬件驱动成功了。

---

## 五、自测 UDS 诊断

### 5.1 通过 CAN 总线自测

程序运行后，新开一个串口窗口（或后台运行 `./can_service &`），然后用 `cansend` 发 UDS 请求：

```bash
# 切换扩展会话（写数据需要）
cansend can0 7E0#1003
# 预期响应: can0 7E8 [2] 50 03

# 读取发动机转速 (DID 0xF100)
cansend can0 7E0#22F100
# 预期响应: can0 7E8 [7] 62 F1 00 00 00 00 00
#          62 = 肯定响应 (0x22+0x40)
#          F1 00 = DID
#          00 00 00 00 = float(0.0)（因为没有外部报文解码写入值）

# 读取 VIN (DID 0xF190)
cansend can0 7E0#22F190
# 预期响应: can0 7E8 [1+2+17] 62 F1 90 AUTOCORE123456789

# 请求种子 (安全访问第一步)
cansend can0 7E0#2701
# 预期响应: can0 7E8 [6] 67 01 [4字节种子]

# 发送密钥 (seed ^ 0x12345678)
cansend can0 7E0#2702[你计算的密钥]
# 预期响应: can0 7E8 [2] 67 02  (解锁成功)
```

### 5.2 通过以太网 DoIP 自测

板子通过网线连到你的 PC（或局域网）。需要设置板子的 IP：

```bash
# 板子上设置 IP
ifconfig eth1 192.168.1.100 up

# 确认板子 IP
ifconfig eth1
```

PC 上安装 Python（不需要 CAN 硬件，用 TCP 即可）：

```python
# 保存为 doip_test.py，在 PC 上运行
import socket
import struct

def send_uds(sock, data):
    """发送 UDS 请求并打印响应"""
    src_addr = 0x0000
    tgt_addr = 0x0E80
    
    # 构建 DoIP 诊断消息
    payload = struct.pack('!HH', src_addr, tgt_addr) + bytes(data)
    header = struct.pack('!BBHI', 0x03, 0xFC, 0x8001, len(payload))
    sock.sendall(header + payload)
    
    # 接收响应
    resp = sock.recv(4096)
    print(f"Response ({len(resp)} bytes): {resp.hex()}")
    return resp

# 连接
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.settimeout(5)
sock.connect(("192.168.1.100", 13400))

# 1. 路由激活
routing_req = struct.pack('!HBBBBB', 0x0000, 0x01, 0x00, 0x00, 0x00, 0x00)
header = struct.pack('!BBHI', 0x03, 0xFC, 0x0005, len(routing_req))
sock.sendall(header + routing_req)
resp = sock.recv(4096)
print(f"Routing activation: {resp.hex()}")

# 2. 读发动机转速 (22 F1 00)
send_uds(sock, [0x22, 0xF1, 0x00])

# 3. 读 VIN (22 F1 90)
send_uds(sock, [0x22, 0xF1, 0x90])

# 4. 读 DTC 数量 (19 01)
send_uds(sock, [0x19, 0x01])

sock.close()
```

---

## 六、关键观察点

### 6.1 程序输出的关键行

```
[RTE] CAN interface: can0 (override with CAN_IF env var)
```

- **有这一行**：新代码生效，支持环境变量切换
- **如果没有这一行**：你在运行旧版程序，需要重新编译

```
[RTE] CAN interface 'can0' opened (fd=3)
```

- **有这一行**：CAN 硬件驱动成功
- **没有这一行，而是 `can_open(vcan0) failed`**：要么在运行旧版，要么 `can0` 没配置好

### 6.2 CAN 总线能看到自己发的报文

回环模式下：`cansend can0 7E0#22F100` 后，`candump` 能同时看到请求和响应。

```
  can0  7E0  [3] 22 F1 00       ← 你发的请求
  can0  7E8  [7] 62 F1 00 ...   ← 网关自动回复的响应
```

### 6.3 诊断报文路由验证

新代码中，诊断 ID（0x7E0/0x7E8）被 `can_router_poll()` 路由到 `isotp_rx_frame()`，**不会**经过普通 CAN 报文路径。所以收到 UDS 请求时，不会有 `[SWC] DEBUG: CAN ID 0x7E0` 的打印——只有 `[ISO_TP] RX SF` 和 `[UDS] Request`。

---

## 七、如果遇到问题

| 症状 | 检查 |
|------|------|
| `can0` 配置失败 | `lsmod`, `dmesg`, `/lib/modules/` 下有没有 flexcan |
| 程序运行没输出 | 串口终端是否设置正确（115200） |
| 程序输出 `CAN disabled` | 1) 旧程序？ 2) can0 没配好？ |
| 发 UDS 请求没响应 | 1) 程序在运行？ 2) 能收到对应的 CAN 帧？(`candump can0`) |
| 发 UDS 请求看到响应但不对 | 检查 ISO-TP 帧格式（SF=1 字节 PCI, FF=2 字节 PCI） |
| DoIP 连不上 | 板子 IP 设置了吗？`ifconfig eth1` |
