# AutoCore 分层架构从零教程

> 本文档写给**没做过车载开发的初学者**，用类比 + 代码实际调用链来解释为什么 AutoCore 要这样分层，每一层做什么，面试会怎么问。

---

## 一、为什么需要分层？——餐馆类比

想象你开一家餐馆：

```
┌─────────────────────────────────────┐
│  顾客点餐（菜单上写 "牛排"）         │  ← 这是应用层
├─────────────────────────────────────┤
│  服务员把订单传给厨房                 │  ← 这是 RTE
├─────────────────────────────────────┤
│  厨房：切肉、调味、煎制               │  ← 这是 BSW
├─────────────────────────────────────┤
│  灶台：控制火力大小                   │  ← 这是 MCAL
└─────────────────────────────────────┘
```

**如果不分层会怎样？**

顾客直接喊："把灶台开到 180 度，煎 3 分钟！"——顾客需要懂厨艺。如果明天换电磁炉，所有顾客都得重新学。

**分层后：**

- 顾客只说"我要牛排"（应用层只管业务逻辑）
- 服务员传单（RTE 做中间翻译）
- 厨房决定怎么煎（BSW 实现具体功能）
- 灶台只管加热（MCAL 抽象硬件差异）

**换了灶台（从燃气换电磁炉）→ 只改 MCAL 一层，上面都不动。**

这就是 AutoCore 分层的核心价值。

---

## 二、AutoCore 的四层架构

### 整体鸟瞰

```
┌──────────────────────────────────────────────────┐
│                    app/                           │
│  应用层 (Application Layer / SWC)                  │
│  只关心："收到 CAN 报文了，车速是多少？"             │
│  不关心："CAN 报文是怎么收上来的？"                  │
│                                                    │
│  文件: can_service/src/main.c                      │
│        uds_service/src/uds_service.c               │
├──────────────────────────────────────────────────┤
│                    rte/                            │
│  RTE (Runtime Environment)                         │
│  做翻译：应用层说 "我要读车速"                       │
│          RTE 去 BSW 拿数据，返回给应用层              │
│                                                    │
│  文件: rte/src/rte.c                               │
│        rte/include/rte.h                           │
│        rte/include/rte_vehicle_data.h              │
├──────────────────────────────────────────────────┤
│                    bsw/                            │
│  基础软件层 (Basic Software Layer)                  │
│  真正的"干活"层——CAN 收发、诊断协议、TCP 通信         │
│                                                    │
│  ┌──────┬────────┬───────┬──────────┐              │
│  │ can/ │ diag/  │tcp_ip/│ sys/     │              │
│  │      │        │       │ mem/     │              │
│  └──────┴────────┴───────┴──────────┘              │
├──────────────────────────────────────────────────┤
│                   bsw/mcal/                        │
│  微控制器抽象层 (MCAL)                              │
│  直接和硬件打交道：socket()、bind()、write()          │
│  如果换 CAN 硬件（比如从 SocketCAN 换 MCP2515），    │
│  只需要重写这一层。                                 │
│                                                    │
│  文件: bsw/mcal/can/src/can_driver.c                │
└──────────────────────────────────────────────────┘
```

### 原理：每一层的依赖方向

**关键规则：上层可以调用下层，下层不能调用上层。**

```
app/  →  rte/  →  bsw/  →  bsw/mcal/
  ↕         ↕         ↕          ↕
只能向下    只能向下   只能向下    最底层
调用        调用       调用        (调不了谁)
```

这个规则保证：换了底层硬件，上层代码一行都不用改。

---

## 三、每一个文件是做什么的？（完整列表）

### 3.1 应用层 (app/)

| 文件 | 行数 | 做什么 |
|------|------|--------|
| `app/can_service/src/main.c` | 178 | **入口**。初始化 RTE → 注册 CAN 回调 → 启动 UDS/DoIP → 主循环接收诊断请求 |
| `app/uds_service/src/uds_service.c` | 123 | UDS 服务 SWC，处理诊断业务逻辑 |

### 3.2 RTE 层 (rte/)

| 文件 | 行数 | 做什么 |
|------|------|--------|
| `rte/include/rte.h` | 99 | **应用层唯一能包含的头文件**。定义 Rte_Init/Rte_Run/Rte_IRead_* 等接口 |
| `rte/include/rte_vehicle_data.h` | 16 | 车辆数据结构体（engine_speed, vehicle_speed, engine_temp） |
| `rte/src/rte.c` | 138 | RTE 实现：打开 CAN、轮询、回调注册、数据读写 |
| `rte/src/rte_vehicle_data.c` | 2 | 定义全局变量 g_vehicle_data |

### 3.3 BSW — CanIf (CAN 接口层)

| 文件 | 行数 | 做什么 |
|------|------|--------|
| `bsw/can/can_if/include/can_router.h` | 17 | **CanIf 的外层接口**。应用层通过 RTE 间接调用这里 |
| `bsw/can/can_if/src/can_router.c` | 45 | 路由器：串联调度、接收、过滤、分发、信号解码 |
| `bsw/can/can_if/include/can_dispatcher.h` | 8 | 分发器接口 |
| `bsw/can/can_if/src/can_dispatcher.c` | 37 | 收到报文后，根据 CAN ID 调用对应的回调函数 |
| `bsw/can/can_if/include/can_filter.h` | 7 | 过滤器接口 |
| `bsw/can/can_if/src/can_filter.c` | 32 | 只处理我们感兴趣的 CAN ID，其他的忽略 |
| `bsw/can/can_if/include/can_scheduler.h` | 17 | 调度器接口 |
| `bsw/can/can_if/src/can_scheduler.c` | 69 | 周期性发送 CAN 报文（比如心跳 0x700） |
| `bsw/can/can_if/include/can_signal.h` | 25 | 信号解码描述（哪些 bit 对应什么信号） |
| `bsw/can/can_if/src/can_signal.c` | 52 | 从原始 CAN 数据中提取物理值（如转速、车速） |
| `bsw/can/can_if/include/can_message.h` | 8 | 消息处理接口 |
| `bsw/can/can_if/src/can_message.c` | 21 | 消息处理入口 |
| `bsw/can/can_if/include/can_message_handler.h` | 8 | 消息处理器接口 |
| `bsw/can/can_if/src/can_message_handler.c` | 27 | 收到报文后调用 can_decode() 并更新到 g_vehicle_data |
| `bsw/can/can_if/include/can_db.h` | 7 | CAN 信号数据库声明 |
| `bsw/can/can_if/src/can_db.c` | 29 | 定义哪些 CAN ID 上有哪些信号（engine_speed 在 0x100 的 bit 0-15） |
| `bsw/can/can_if/include/can_message_db.h` | 25 | 消息数据库接口 |
| `bsw/can/can_if/src/can_message_db.c` | 49 | 消息数据库实现 |

### 3.4 BSW — MCAL (微控制器抽象层)

| 文件 | 行数 | 做什么 |
|------|------|--------|
| `bsw/mcal/can/include/can_driver.h` | 11 | 定义 can_open/can_send/can_recv/can_close 四个函数 |
| `bsw/mcal/can/src/can_driver.c` | 81 | **真正的 Linux SocketCAN 驱动**：socket() → ioctl() → bind() |

### 3.5 BSW — 诊断协议 (ISO-TP + UDS)

| 文件 | 行数 | 做什么 |
|------|------|--------|
| `bsw/diag/iso_tp/include/iso_tp.h` | 134 | ISO 15765-2 接口：帧类型定义、SF/FF/FC/CF 常量 |
| `bsw/diag/iso_tp/src/iso_tp.c` | 424 | **传输层**：长报文拆帧/组帧状态机。如果报文 ≤7 字节发 SF，否则发 FF+FC+CF |
| `bsw/diag/uds/include/uds.h` | 188 | UDS 协议定义：6 个 SID、NRC、DID、会话/安全状态 |
| `bsw/diag/uds/src/uds_core.c` | 593 | **UDS 诊断引擎**：实现 0x10/0x22/0x2E/0x27/0x19/0x14 六个服务 |

### 3.6 BSW — TCP/IP 服务 (DoIP)

| 文件 | 行数 | 做什么 |
|------|------|--------|
| `bsw/tcp_ip/socket_adapter/include/socket_adapter.h` | 113 | TCP/UDP socket 封装 |
| `bsw/tcp_ip/socket_adapter/src/socket_adapter.c` | 202 | 基于 select 的非阻塞 TCP server/client 封装 |
| `bsw/tcp_ip/doip/include/doip.h` | 157 | DoIP 协议定义：车辆发现、路由激活、诊断消息 |
| `bsw/tcp_ip/doip/src/doip.c` | 404 | **DoIP 服务器**：UDP 广播 + TCP 诊断消息转发到 UDS 引擎 |

### 3.7 BSW — 系统服务

| 文件 | 行数 | 做什么 |
|------|------|--------|
| `bsw/sys/logger/include/logger.h` | 4 | 日志接口 |
| `bsw/sys/logger/src/logger.c` | 16 | 简易日志打印 |

---

## 四、函数调用链路图（最重要的部分）

### 4.1 启动流程

```
main()                                          ← app/can_service/src/main.c
 │
 ├─ Rte_Init()                                  ← rte/src/rte.c
 │    │
 │    └─ can_open("vcan0")                      ← bsw/mcal/can/src/can_driver.c
 │         │
 │         ├─ socket(PF_CAN, SOCK_RAW, CAN_RAW)  ← Linux 内核 API
 │         ├─ ioctl(fd, SIOCGIFINDEX)            ← 找到接口索引
 │         └─ bind(fd, ...)                      ← 绑定到 can0/vcan0
 │
 ├─ Rte_Call_RegisterRxCallback(0x100, cb)      ← rte/src/rte.c
 │    │
 │    └─ can_router_register(0x100, cb)          ← bsw/can/can_if/src/can_router.c
 │         │
 │         └─ can_register(0x100, cb)            ← bsw/can/can_if/src/can_dispatcher.c
 │              │
 │              └─ dispatcher 表里加一条记录
 │
 ├─ Rte_Call_SendCanFrame(0x700, data, 1)       ← rte/src/rte.c
 │    │
 │    └─ can_send(fd, &frame)                   ← bsw/mcal/can/src/can_driver.c
 │         │
 │         └─ write(fd, frame, sizeof(frame))    ← Linux 内核 SYSCALL
 │
 ├─ uds_init(0x7E0, 0x7E8)                      ← bsw/diag/uds/src/uds_core.c
 │    │
 │    └─ 设置 request_id=0x7E0, response_id=0x7E8
 │
 └─ doip_init(&doip, "AUTOCORE123456789", 0x0E80) ← bsw/tcp_ip/doip/src/doip.c
      │
      ├─ socket(AF_INET, SOCK_DGRAM, 0)          ← UDP 车辆发现
      ├─ bind(udp_fd, port=13400)
      ├─ socket(AF_INET, SOCK_STREAM, 0)         ← TCP 诊断消息
      ├─ bind(tcp_fd, port=13400)
      └─ listen(tcp_fd, 1)
```

### 4.2 主循环（每毫秒执行一次）

```
while (1) {
    Rte_Run();                                      ← rte/src/rte.c
    │
    └─ can_router_poll()                           ← bsw/can/can_if/src/can_router.c
         │
         ├─ can_scheduler_run(fd)                  ← bsw/can/can_if/src/can_scheduler.c
         │    │
         │    └─ 遍历 items[]:
         │         如果 (now - last_run) >= period_ms:
         │             can_send(fd, &frame)         ← bsw/mcal/can/src/can_driver.c
         │                 └─ write(fd, ...)        ← Linux SYSCALL
         │
         └─ can_recv(fd, &rx_frame)                ← bsw/mcal/can/src/can_driver.c
              │
              └─ read(fd, ...)                     ← Linux SYSCALL
                   │
                   └─ 如果有数据:
                        │
                        ├─ can_filter_match(id)    ← bsw/can/can_if/src/can_filter.c
                        │    └─ 检查这个 CAN ID 是不是我们注册过的
                        │
                        ├─ can_dispatch(&frame)    ← bsw/can/can_if/src/can_dispatcher.c
                        │    └─ 遍历 handlers[]:
                        │        如果 handler.can_id == frame.can_id:
                        │            handler.cb(&frame)    ← 调用回调
                        │
                        └─ can_message_process(&frame) ← bsw/can/can_if/src/can_message.c
                             │
                             └─ test_message_handler(&frame) ← bsw/can/can_if/src/can_message_handler.c
                                  │
                                  └─ can_decode(frame, &sig) ← bsw/can/can_if/src/can_signal.c
                                       │
                                       └─ extract_bits(frame, start_bit, length)
                                            │
                                            └─ 从 data[] 中提取 N 个 bit，换算成物理值
                                            │
                                            └─ g_vehicle_data.engine_speed = sig.engine_speed  ← 写入 RTE 共享数据

    /* CAN 诊断入口 */
    isotp_receive(&rx_id, buf, sizeof(buf))        ← bsw/diag/iso_tp/src/iso_tp.c
    │
    └─ 返回 0 (没有完整报文) 或 >0 (收到一个完整的 ISO-TP 报文)
         │
         └─ uds_handle_request(buf, len, &resp)    ← bsw/diag/uds/src/uds_core.c
              │
              └─ switch (buf[0]):  // SID
                   ├─ 0x10 → 会话控制
                   ├─ 0x22 → 读取 DID
                   ├─ 0x2E → 写入 DID
                   ├─ 0x27 → 安全访问
                   ├─ 0x19 → 读 DTC
                   └─ 0x14 → 清 DTC

    /* DoIP 诊断入口 */
    doip_poll(&doip, uds_handler)                  ← bsw/tcp_ip/doip/src/doip.c
    │
    ├─ select(max_fd+1, &read_fds, ...)
    │
    ├─ UDP 收到车辆发现请求 → 回复 VIN + 逻辑地址
    │
    ├─ TCP 收到新连接 → accept() → 设为 diagnostic_fd
    │
    └─ TCP 收到诊断消息 → 解析 DoIP 头
         │
         └─ uds_callback(uds_data, uds_len, resp, &resp_len)
              │
              └─ uds_handle_request(req, req_len, &response)
}
```

### 4.3 信号解码过程（面试重点）

这是一个最关键的链路——CAN 总线上收到原始字节，如何变成应用层能用的"车速"：

```
CAN 总线收到: 0x100 | 0x41 0xE8 0x00 0x00 ...

      ↓ can_recv() → read(fd, &frame)

struct can_frame {
    can_id = 0x100,           // 发动机状态报文
    can_dlc = 8,
    data[0] = 0x41,           // bit 0-7
    data[1] = 0xE8,           // bit 8-15
    data[2..7] = ...
}

      ↓ can_signal.c → extract_bits()

查找 can_db[] 中 can_id==0x100 的信号定义：
  - engine_speed:  start_bit=0, length=16 bits, factor=0.125, offset=0

从 frame.data[0..1] 提取 16 bits:
  raw = (0x41 << 8) | 0xE8 = 0x41E8 = 16872
  physical = 16872 * 0.125 + 0 = 2109.00 rpm

      ↓ can_message_handler.c

g_vehicle_data.engine_speed = 2109.00

      ↓ main.c 的回调

Rte_IRead_EngineSpeed() → 返回 2109.00

      ↓ 应用层

printf("engine_speed = %.2f rpm\n", 2109.00);
```

---

## 五、诊断双通道入口

AutoCore 支持**两种**诊断入口，面试常问：

```
诊断仪
    │
    ├── CAN 通道 ── can0 ──► ISO-TP ──► UDS ──► 响应
    │                       (SF/FF/       (6 个
    │                        FC/CF)       服务)
    │
    └── 以太网通道 ──► DoIP ──► UDS ──► 响应
                       (TCP        (同一套
                        13400)      UDS 引擎)
```

**为什么两种都要？**

| | CAN UDS | DoIP |
|---|---|---|
| 带宽 | 500 kbps | 100 Mbps |
| 适合场景 | 日常诊断、读取故障码 | 固件升级（几 MB 数据） |
| 硬件成本 | 只需 CAN 收发器 | 需要以太网 PHY |
| 标准 | ISO 14229 over ISO 15765-2 | ISO 13400 |

**UDS 引擎是共享的** — 两个通道的请求最终都调用 `uds_handle_request()`，同一个函数处理。

---

## 六、UDS 6 个服务的详细流程

### 6.1 0x10 — 诊断会话控制

```
Tester                            ECU (AutoCore)
  │── 10 03 ──────────────────────→│  请求切换到扩展会话
  │←── 50 03 ──────────────────────│  肯定响应，进入扩展会话
  │                                │  内部: g_session = EXTENDED
  
  │── 10 01 ──────────────────────→│  请求回到默认会话
  │←── 50 01 ──────────────────────│  肯定响应
```

面试问：**为什么需要会话控制？**
> 安全策略。默认会话只能读数据（0x22），不能写（0x2E）。写数据需要扩展或编程会话。这是防止误操作。

### 6.2 0x22 — 读取数据

```
Tester                            ECU
  │── 22 F1 00 ──────────────────→│  读取 DID 0xF100（发动机转速）
  │←── 62 F1 00 [4字节 float] ────│  肯定响应 + 数据
```

读取一个 DID 的流程：

```
uds_handle_request()
  │ 检测 SID = 0x22
  │ 读取 DID = 0xF100
  │
  ├─ 内部 DID? (VIN, SW_VERSION) → 直接返回
  │
  └─ 外部 DID? (ENGINE_SPEED) → 调用 g_read_cb
       │
       └─ read_vehicle_data(0xF100, data, max_len) ← 在 main.c 定义
            │
            └─ Rte_IRead_EngineSpeed() → g_vehicle_data.engine_speed
```

### 6.3 0x27 — 安全访问（面试经典题）

```
Tester                            ECU
  │── 27 01 ──────────────────────→│  请求种子
  │←── 67 01 [4字节 seed] ────────│  ECU 生成随机种子
  │                                │  g_seed = rand()
  │                                │  g_seed_valid = 1
  │                                │
  │── 27 02 [4字节 key] ─────────→│  发送密钥
  │                                │  ECU 计算: expected = seed ^ 0x12345678
  │                                │  如果 key == expected: 解锁
  │                                │  否则: attempt++，3次失败锁定10秒
  │←── 67 02 ─────────────────────│  解锁成功
  │  (或 7F 27 35 → 密钥无效)
  │  (或 7F 27 36 → 超过尝试次数)
```

面试问：**为什么需要安全访问？**
> 防止未经授权的诊断操作（比如刷写 ECU 固件）。种子-密钥机制保证只有知道密钥算法的诊断仪才能操作。

面试问：**种子-密钥算法的缺点？**
> 算法是固定的（seed ^ 0x12345678），一旦被逆向就失效。实际项目中会用更复杂的算法（AES、自定义多项式等）。

### 6.4 0x2E — 写入数据

```
Tester                            ECU
  │── 2E F1 90 [17字节VIN] ──────→│  写入 VIN
  │                                │  检查: 会话 != 默认?
  │                                │  检查: 安全已解锁?
  │←── 6E F1 90 ──────────────────│  写入成功
```

### 6.5 0x19 — 读取故障码

```
Tester                            ECU
  │── 19 01 ──────────────────────→│  报告 DTC 数量
  │←── 59 01 [DTC数量] ───────────│
  │                                │
  │── 19 02 ──────────────────────→│  报告 DTC 列表
  │←── 59 02 [DTC列表] ───────────│  DTC 0x123456: status=0x28
```

### 6.6 0x14 — 清除故障码

```
Tester                            ECU
  │── 14 FF FF FF ───────────────→│  清除所有 DTC
  │←── 54 ────────────────────────│  成功
```

---

## 七、面试问答集锦

### Q1: 为什么要分四层？不分层行不行？

**不分层的代码**（初学者常见写法）：

```c
// 不分层：main.c 直接操作 socket
int main() {
    int fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    ioctl(fd, SIOCGIFINDEX, &ifr);
    bind(fd, ...);
    
    while (1) {
        read(fd, &frame);
        if (frame.can_id == 0x100) {
            float speed = (frame.data[0] << 8 | frame.data[1]) * 0.125;
            printf("speed = %f\n", speed);
        }
    }
}
```

**问题**：
1. 换硬件（比如从 SocketCAN 换 MCP2515 SPI 驱动）→ 重写所有代码
2. 换应用（比如从"读转速"换成"读车速"）→ 需要理解底层 CAN 细节
3. 两个人同时开发 → 互相阻塞
4. 不能复用 → 下个项目重新写

**分层后的代码**：

```c
// main.c — 只关心业务
int main() {
    Rte_Init();
    Rte_Call_RegisterRxCallback(0x100, my_callback);
    while (1) { Rte_Run(); }
}

static void my_callback(uint32_t id, const uint8_t *data, uint8_t len) {
    float speed = Rte_IRead_EngineSpeed();
    printf("speed = %f\n", speed);
}
```

换硬件？改 `bsw/mcal/can/can_driver.c`，main.c 不动。换应用？改 main.c，driver 不动。

### Q2: RTE 到底是什么？不能直接调 BSW 吗？

RTE 是**唯一通道**。应用层代码只包含 `#include "rte.h"`，看不到任何 BSW 头文件。

```
直接调 BSW（不通过 RTE）：

main.c → can_router_register(0x100, cb)  ← 直接调 BSW
       → can_scheduler_add(100, &frame)  ← 直接调 BSW

缺点：如果明天把 can_router_register 改名了，所有应用都得改。
```

```
通过 RTE：

main.c → Rte_Call_RegisterRxCallback(0x100, cb)  ← RTE 包装
       → Rte_Call_SendCanFrame(...)                ← RTE 包装

RTE 内部再调 can_router_register / can_scheduler_add。
BSW 接口变了，只改 RTE 一层。
```

**生活类比**：RTE 就像公司的"接口部门"。客户（应用层）想提需求，不需要知道是哪个技术团队（BSW 哪个模块）做的，只需要找接口部门（RTE）。

### Q3: can_router.c 为什么只有 45 行？

因为它是一个"胶水模块"——汇聚 6 个子模块的调用：

```
can_router_poll()
  │
  ├── can_scheduler_run(fd)      ← 调度器（周期性发送）
  ├── can_recv(fd, &frame)       ← MCAL 驱动（接收）
  ├── can_filter_match(id)       ← 过滤器
  ├── can_dispatch(&frame)       ← 分发器（调回调）
  └── can_message_process(frame) ← 消息处理（信号解码）
```

如果把这 6 个功能写在一个文件里，那就会变成一个几百行的"上帝文件"。拆开的好处：每个文件只做一件事，容易测试、容易修改。

### Q4: ISO-TP 为什么需要状态机？

因为 CAN 是"发完就忘"的协议，不保证顺序。ISO-TP 需要在单帧/多帧之间跟踪状态。

```
发送状态机：
  IDLE ──(调用 isotp_send)──→ WAIT_FC ──(收到 FC)──→ SENDING_CF ──(发完)──→ IDLE
                                  │                      │
                                  └──(超时)──→ IDLE       └──(超时)──→ IDLE

接收状态机：
  IDLE ──(收到 FF)──→ WAITING_CF ──(收完 CF)──→ IDLE
                           │
                           └──(超时)──→ IDLE
```

面试问：**ISO-TP 超时了怎么办？**
> 发送方超时（N_As/N_Bs）→ 放弃本次发送，回到 IDLE。接收方超时（N_Cr）→ 丢弃已收到的部分数据，回到 IDLE。上层（UDS）会检测到请求无响应。

### Q5: DoIP 和 CAN UDS 的关系？

DoIP 传输层 = TCP/IP，CAN 传输层 = ISO-TP。**UDS 应用层是共享的**。

```
CAN 诊断路径:
  CAN 总线 → can_recv() → isotp_poll() → UDS 引擎 → isotp_send()

DoIP 诊断路径:
  以太网 → select() → doip_poll() → UDS 引擎 → send_doip_message()
```

所以扩展一个新 UDS 服务，CAN 和 DoIP 通道都能自动支持。

### Q6: NVRAM 为什么重要（还没实现）？

当前 VIN 码是编译时写死的 `"AUTOCORE123456789"`。重启后就丢了。

NVRAM 的作用：
1. **VIN 持久化**：通过 UDS 0x2E 写入的 VIN 掉电不丢失
2. **DTC 持久化**：故障码记录在 EEPROM/文件里，下次上电还能读
3. **配置参数**：CAN 比特率、节点地址等

---

## 八、如果我想自己从头写，步骤是什么？

```
Step 1: 搭骨架
  ├── CMakeLists.txt（顶层 + 每个模块一个）
  ├── main.c（空循环）
  └── make.sh（编译脚本）

Step 2: 加日志（sys/logger）
  └── 调试必备，printf 包装

Step 3: 加 MCAL CAN 驱动
  └── can_open/can_send/can_recv/can_close
  └── 测试：在 PC 上配 vcan0，能收发包

Step 4: 加 CanIf 层
  ├── can_router（入口）
  ├── can_dispatcher（分发）
  ├── can_filter（过滤）
  ├── can_scheduler（调度）
  ├── can_signal（信号解码）
  └── can_message_handler（信号→数据）

Step 5: 加 RTE
  ├── rte.h（应用层接口）
  ├── rte_vehicle_data.h（全局数据）
  └── rte.c（实现）

Step 6: 加 ISO-TP
  └── 单帧/多帧状态机

Step 7: 加 UDS
  └── 6 个核心服务

Step 8: 加 DoIP
  └── UDP 发现 + TCP 诊断

Step 9: 交叉编译 → 部署到 I.MX6ull
```

---

## 九、调试技巧

### 在 PC 上开发（不需要板子）

```bash
# 1. 创建虚拟 CAN 接口
sudo modprobe can
sudo modprobe can_raw
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set vcan0 up

# 2. 编译运行
./make.sh
./build/app/can_service/can_service

# 3. 另一个终端发送 CAN 报文
sudo apt install can-utils
cansend vcan0 100#41E80000   # 发送发动机转速报文
cansend vcan0 7E0#22F100     # 发送 UDS 诊断请求（读发动机转速）
```

### 跟踪函数调用

```bash
# 用 gdb 设置断点
gdb ./build/app/can_service/can_service
(gdb) b can_router_poll
(gdb) b can_dispatch
(gdb) b can_decode
(gdb) run
```

或者简单粗暴——看 printf 输出。所有模块都有 `[RTE]` `[SWC]` `[UDS]` `[DoIP]` 前缀。

---

## 十、总结图

```
┌──────────────────────────────────────────────────────────────────┐
│ app/can_service/src/main.c                                        │
│                                                                   │
│  只知道：CAN_ID 0x100 = 发动机状态报文                             │
│         DID 0xF100 = 发动机转速                                   │
│                                                                   │
│  通过 RTE 接口（rte.h）做所有事情                                  │
├──────────────────────────────────────────────────────────────────┤
│ rte/                                                              │
│                                                                   │
│  Rte_Init()           → can_open() + can_router_init()            │
│  Rte_Run()            → can_router_poll()                        │
│  Rte_IRead_*()        → g_vehicle_data 读数据                    │
│  Rte_Call_*()         → can_send() / can_router_register()       │
│                                                                   │
│  隐藏了所有 BSW 细节                                              │
├──────────────────────────────────────────────────────────────────┤
│ bsw/can/can_if/          bsw/diag/      bsw/tcp_ip/              │
│                                                                   │
│  can_router    ────调度──→  scheduler                              │
│       │                                                           │
│       └───────接收──→  filter → dispatch → signal_decode          │
│                                                                    │
│  iso_tp ──── 拆帧/组帧 ──── uds (6 服务)                          │
│  doip  ──── TCP/UDP ──── uds (同一套引擎)                         │
├──────────────────────────────────────────────────────────────────┤
│ bsw/mcal/can/                                                     │
│                                                                   │
│  can_driver.c: socket() → ioctl() → bind() → read()/write()      │
│                                                                   │
│  唯一知道 "vcan0" 这个名字的模块                                  │
└──────────────────────────────────────────────────────────────────┘
```

---

> **学习方法建议**：
> 1. 先在 PC 上 `./make.sh` 编译，`cansend vcan0 ...` 发报文看输出
> 2. 在 `can_router_poll()` 里加 printf，看每一帧的调用链
> 3. 理解一个完整的 CAN 报文从收到 → 过滤 → 分发 → 解码 → 应用层使用的路径
> 4. 然后再看 ISO-TP 和 UDS，理解"为什么需要传输层"
> 5. 最后理解 DoIP 是"另一种传输通道"
