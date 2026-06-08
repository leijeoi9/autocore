# AutoCore 完整调用链路教程

> 本教程通过**三条完整的调用链路**，逐行解释代码的执行路径。适合已了解基础概念但对具体函数调用关系感到困惑的初学者。

---

## 目录

- [一、三条链路总览](#一三条链路总览)
- [二、链路一：CAN 报文接收 + 信号解码](#二链路一can-报文接收--信号解码)
  - [第 1 步：主循环入口](#第-1-步主循环入口)
  - [第 2 步：Rte_Run → can_router_poll](#第-2-步rte_run--can_router_poll)
  - [第 3 步：can_recv — MCAL 读硬件](#第-3-步can_recv--mcal-读硬件)
  - [第 4 步：can_filter_match — 过滤器](#第-4-步can_filter_match--过滤器)
  - [第 5 步：can_dispatch — BSW 层回调分发](#第-5-步can_dispatch--bsw-层回调分发)
  - [第 6 步：RTE 适配器 — 格式转换](#第-6-步rte-适配器--格式转换)
  - [第 7 步：应用层 SWC 回调](#第-7-步应用层-swc-回调)
  - [第 8 步：can_message_process — 消息处理](#第-8-步can_message_process--消息处理)
  - [第 9 步：can_decode — 信号解码（最核心）](#第-9-步can_decode--信号解码最核心)
  - [第 10 步：全局数据更新](#第-10-步全局数据更新)
  - [完整流程图](#完整流程图)
- [三、链路二：UDS 诊断请求（DoIP 通道）](#三链路二uds-诊断请求doip-通道)
  - [第 1 步：doip_poll 检测 TCP 数据](#第-1-步doip_poll-检测-tcp-数据)
  - [第 2 步：DoIP 协议解析](#第-2-步doip-协议解析)
  - [第 3 步：UDS 引擎 dispatch](#第-3-步uds-引擎-dispatch)
  - [第 4 步：handle_read_data 内部](#第-4-步handle_read_data-内部)
  - [第 5 步：读取实时车辆数据](#第-5-步读取实时车辆数据)
  - [第 6 步：响应回传](#第-6-步响应回传)
- [四、链路三：ISO-TP 多帧传输](#四链路三iso-tp-多帧传输)
  - [4.1 为什么需要 ISO-TP](#41-为什么需要-iso-tp)
  - [4.2 发送多帧的全过程](#42-发送多帧的全过程)
  - [4.3 接收多帧的全过程](#43-接收多帧的全过程)
- [五、回调系统详解（难点）](#五回调系统详解难点)
  - [5.1 三层回调体系](#51-三层回调体系)
  - [5.2 回调注册流程](#52-回调注册流程)
  - [5.3 为什么这样设计](#53-为什么这样设计)
- [六、当前架构的问题](#六当前架构的问题)
  - [6.1 CAN 通道的 UDS 诊断不可用](#61-can-通道的-uds-诊断不可用)
  - [6.2 回调路径重复](#62-回调路径重复)

---

## 一、三条链路总览

| 链路 | 入口 | 触发方式 | 终点 | 说明 |
|------|------|----------|------|------|
| **链路一** | `while(1) { Rte_Run(); }` | CAN 总线收到报文 | 应用层回调 + 信号解码 | 正常 CAN 通信流程 |
| **链路二** | `while(1) { doip_poll(); }` | TCP/13400 收到诊断消息 | UDS 引擎 → 响应 | DoIP 以太网诊断 |
| **链路三** | ISO-TP 多帧收发 | 报文 > 7 字节 | SF/FF/FC/CF 状态机 | 传输层协议 |

---

## 二、链路一：CAN 报文接收 + 信号解码

> **场景**：你在 PC 上执行 `cansend vcan0 123#AABBCCDDEEFF0011`，程序收到后解码发动机转速并打印。

### 第 1 步：主循环入口

**文件**：`app/can_service/src/main.c` 第 153-175 行

```c
while (1) {
    /* 驱动 RTE（底层 = can_router_poll + 调度器） */
    Rte_Run();                                                    // ← 第 158 行

    /* --- 诊断入口 1: ISO-TP (CAN) --- */
    int len = isotp_receive(&rx_id, uds_request_buf,
                             sizeof(uds_request_buf));            // ← 第 161 行
    if (len > 0) {
        uds_response_t response;
        uds_handle_request(uds_request_buf, (uint16_t)len, &response);
        isotp_send(uds_get_response_id(), response.data, response.len);
    }

    /* --- 诊断入口 2: DoIP (以太网) --- */
    doip_poll(&doip, doip_uds_handler);                           // ← 第 172 行

    usleep(1000);                                                 // ← 第 174 行
}
```

关键点：**每 1ms 循环一次**，每次做三件事：
1. `Rte_Run()` — 驱动 CAN 通信栈
2. `isotp_receive()` — 检查是否有完整的 ISO-TP 报文（诊断请求走 CAN）
3. `doip_poll()` — 检查是否有 DoIP 诊断请求（诊断走以太网）

---

### 第 2 步：Rte_Run → can_router_poll

**文件**：`rte/src/rte.c` 第 44-50 行

```c
void Rte_Run(void)
{
    if (rte_can_fd < 0)                       // ← CAN 没打开就跳过（板子上无 vcan 就走这里）
        return;

    /* 内部调用 BSW 的轮询接口 */
    can_router_poll();                        // ← 第 49 行
}
```

**文件**：`bsw/can/can_if/src/can_router.c` 第 33-45 行

```c
void can_router_poll(void)
{
    can_scheduler_run(g_fd);                  // ← 第 35 行：检查是否需要周期性发送

    if (can_recv(g_fd, &rx_frame) > 0) {      // ← 第 37 行：读 CAN 帧
                                              //    rx_frame 是全局 static 变量

        if (can_filter_match(rx_frame.can_id)) {    // ← 第 39 行：只处理关心的 ID
            can_dispatch(&rx_frame);                // ← 第 41 行：调已注册的回调
            can_message_process(&rx_frame);         // ← 第 43 行：按消息类型处理
        }
    }
}
```

这是**整个 CAN 通信的枢纽点**——一个函数串联了 5 个子模块：

```
can_router_poll()
  │
  ├── ① can_scheduler_run()    → 调度器：检查周期报文是否需要发送
  ├── ② can_recv()              → MCAL：从 Linux 内核读一帧
  ├── ③ can_filter_match()     → 过滤器：这个 ID 我们关心吗？
  ├── ④ can_dispatch()         → 分发器：通知注册的回调
  └── ⑤ can_message_process()  → 消息处理：信号解码 + 更新全局数据
```

---

### 第 3 步：can_recv — MCAL 读硬件

**文件**：`bsw/mcal/can/src/can_driver.c` 第 28-43 行

```c
int can_recv(int fd, struct can_frame *frame)
{
    int nbytes = read(fd, frame, sizeof(struct can_frame));
    //                      ↑  Linux 系统调用
    //                        从 SocketCAN 内核驱动读取
    //                        填充 struct can_frame

    if (nbytes < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)   // 非阻塞模式，没数据
            return 0;
        perror("can_recv");
        return -1;
    }

    return nbytes;       // 成功返回正数，frame 已填充
}
```

返回后，`rx_frame`（`can_router.c` 的静态变量）内容举例：

```
rx_frame = {
    .can_id  = 0x123,           // CAN ID
    .can_dlc = 8,               // 数据长度
    .data    = {0xAA, 0xBB,     // 原始 8 字节
                0xCC, 0xDD,
                0xEE, 0xFF,
                0x00, 0x11}
}
```

⚠️ **重要**：`can_recv()` 通过 `read()` 从 SocketCAN 内核缓冲区消费了一帧数据。**这一帧被读走了，下次再 `read()` 就没了。**

---

### 第 4 步：can_filter_match — 过滤器

**文件**：`bsw/can/can_if/src/can_filter.c` 第 24-32 行

```c
int can_filter_match(int can_id)
{
    for (int i = 0; i < filter_count; i++) {
        if (filter_ids[i] == can_id)
            return 1;               // 匹配！
    }
    return 0;                       // 不关心这个 ID
}
```

`filter_ids[]` 怎么来的？在 `main.c` 第 126-128 行：

```c
Rte_Call_RegisterRxCallback(0x100, engine_status_callback);    // 注册 0x100
Rte_Call_RegisterRxCallback(0x200, vehicle_status_callback);   // 注册 0x200
Rte_Call_RegisterRxCallback(0x123, debug_handler_callback);    // 注册 0x123
```

每个注册内部（`rte.c` 第 117-121 行）：

```c
void Rte_Call_RegisterRxCallback(int can_id, ...)
{
    // 保存到 rte 自己的回调列表
    swc_callbacks[swc_callback_count].can_id = can_id;
    swc_callbacks[swc_callback_count].callback = callback;
    swc_callback_count++;

    // 注册到 BSW 的 dispatcher
    can_router_register(can_id, rte_internal_bsw_adapter);

    // 同时加到过滤器
    can_router_add_filter(can_id);   // → can_filter_add(can_id) → filter_ids[] 加入
}
```

所以发 `0x123` → `filter_match(0x123)` → **匹配** → 继续执行。

---

### 第 5 步：can_dispatch — BSW 层回调分发

**文件**：`bsw/can/can_if/src/can_dispatcher.c` 第 28-37 行

```c
void can_dispatch(const struct can_frame *frame)
{
    for (int i = 0; i < handler_count; i++) {
        if (can_filter_match(frame->can_id) &&       // 再检查一次过滤
            handlers[i].can_id == frame->can_id)     // ID 精确匹配
        {
            handlers[i].cb(frame);                   // ← 调用回调函数！
        }
    }
}
```

`handlers[]` 里注册了什么？

回顾 `Rte_Call_RegisterRxCallback` 内部（`rte.c` 第 117 行）：

```c
can_router_register(can_id, rte_internal_bsw_adapter);
```

即注册到 dispatcher 的回调是 **`rte_internal_bsw_adapter`**，而不是应用层的 `debug_handler_callback`。

所以这里调用的是 `rte_internal_bsw_adapter(&rx_frame)`。

---

### 第 6 步：RTE 适配器 — 格式转换

**文件**：`rte/src/rte.c` 第 90-99 行

```c
/** 内部使用的 BSW 层回调适配器 */
static void rte_internal_bsw_adapter(const struct can_frame *frame)
{
    for (int i = 0; i < swc_callback_count; i++) {
        if (swc_callbacks[i].can_id == (int)frame->can_id) {
            // 将 BSW 的 struct can_frame 转换成简单的 (id, data, len) 三元组
            // 应用层不需要知道 Linux 内核的 can_frame 结构
            swc_callbacks[i].callback(frame->can_id,       // ← 应用层回调
                                       frame->data,
                                       frame->can_dlc);
        }
    }
}
```

**为什么要有这层适配？**

| 层 | 数据结构 | 说明 |
|----|---------|------|
| BSW | `struct can_frame` | Linux 内核定义，包含 `can_id`, `can_dlc`, `data[8]` 等 |
| RTE 转换 | `(uint32_t id, uint8_t *data, uint8_t len)` | 简单三元组，无平台相关性 |
| 应用层 | 业务语义 | `engine_speed_callback(id, data, len)` |

如果从 SocketCAN 换到 MCP2515 SPI 驱动，`struct can_frame` 可能变成不同的结构体。RTE 适配器可以屏蔽这个变化。

---

### 第 7 步：应用层 SWC 回调

**文件**：`app/can_service/src/main.c` 第 49-56 行

```c
static void debug_handler_callback(uint32_t can_id,
                                    const uint8_t *data,
                                    uint8_t len)
{
    printf("[SWC] DEBUG: CAN ID 0x%X, len=%d\n", can_id, len);
    for (uint8_t i = 0; i < len && i < 8; i++) {
        printf("[SWC]   data[%d] = 0x%02X\n", i, data[i]);
    }
}
```

这是应用层 SWC（Software Component）的**可运行实体（Runnable Entity）**。在 AUTOSAR 术语中，这就是"应用层代码"——它只知道 `(id, data, len)`，不知道什么是 `struct can_frame`，不知道什么是 `socket`。

---

### 第 8 步：can_message_process — 消息处理

与此同时，`can_router_poll()` 的第 43 行也执行了。这是**第二条并行路径**：

**文件**：`bsw/can/can_if/src/can_message.c` 第 6-21 行

```c
void can_message_process(const struct can_frame *frame)
{
    const can_message_desc_t *desc;

    desc = find_message_desc(frame->can_id);      // 查消息数据库

    if (!desc)
        return;                                   // 没找到 → 跳过

    printf("[MSG] %s (0x%X)\n", desc->name, frame->can_id);

    if (desc->handler)
        desc->handler(frame);                      // 调处理函数
}
```

消息数据库在 `can_message_db.c` 中定义：

**文件**：`bsw/can/can_if/src/can_message_db.c` 第 5-24 行

```c
can_message_desc_t can_message_db[] = {
    {
        .can_id  = 0x100,
        .name    = "ENGINE_STATUS",
        .handler = NULL                    // ← 没有处理函数
    },
    {
        .can_id  = 0x200,
        .name    = "VEHICLE_STATUS",
        .handler = NULL                    // ← 没有处理函数
    },
    {
        .can_id  = 0x123,
        .name    = "TEST_STATUS",
        .handler = test_message_handler    // ← 有处理函数！
    }
};
```

**只对 0x123 有 handler**。所以我们的报文（0x123）会触发：

```c
test_message_handler(&rx_frame);
```

---

### 第 9 步：can_decode — 信号解码（最核心）

**文件**：`bsw/can/can_if/src/can_message_handler.c` 第 9-27 行

```c
static can_signal_t sig;            // 静态变量，一次性用

void test_message_handler(const struct can_frame *frame)
{
    can_decode(frame, &sig);        // ← 第 12 行：从原始 bytes 提取信号

    // 写入 RTE 全局数据
    g_vehicle_data.engine_speed = sig.engine_speed;     // ← 第 14 行
    g_vehicle_data.vehicle_speed = sig.vehicle_speed;   // ← 第 16 行
    g_vehicle_data.engine_temp = sig.engine_temp;       // ← 第 18 行

    printf("[SIGNAL] engine_speed=%.2f rpm\n",  sig.engine_speed);
    printf("[SIGNAL] vehicle_speed=%.2f km/h\n", sig.vehicle_speed);
    printf("[SIGNAL] engine_temp=%.2f C\n",     sig.engine_temp);
}
```

**文件**：`bsw/can/can_if/src/can_signal.c` 第 26-52 行

```c
void can_decode(const struct can_frame *frame, can_signal_t *sig)
{
    sig->engine_speed = 0;
    sig->vehicle_speed = 0;
    sig->engine_temp = 0;

    for (int i = 0; i < can_db_size; i++) {
        if (can_db[i].can_id == frame->can_id) {       // 匹配信号定义

            unsigned int raw = extract_bits(frame,
                                            can_db[i].start_bit,  // 起始 bit
                                            can_db[i].length);    // bit 长度

            float value = (raw * can_db[i].factor) + can_db[i].offset;

            // 填到对应的字段
            if (strcmp(can_db[i].name, "engine_speed") == 0)
                sig->engine_speed = value;
            else if (strcmp(can_db[i].name, "vehicle_speed") == 0)
                sig->vehicle_speed = value;
            else if (strcmp(can_db[i].name, "engine_temp") == 0)
                sig->engine_temp = value;
        }
    }
}
```

信号数据库 `can_db[]` 在 `can_db.c` 中定义：

**文件**：`bsw/can/can_if/src/can_db.c` 第 3-28 行

```c
can_signal_desc_t can_db[] = {
    {
        .can_id    = 0x123,
        .start_bit = 0,           // bit 0 开始
        .length    = 16,          // 16 bits
        .factor    = 0.1,         // 分辨率
        .offset    = 0,           // 偏移
        .name      = "engine_speed"
    },
    {
        .can_id    = 0x123,
        .start_bit = 16,          // bit 16 开始
        .length    = 16,
        .factor    = 0.1,
        .offset    = 0,
        .name      = "vehicle_speed"
    },
    {
        .can_id    = 0x123,
        .start_bit = 32,          // bit 32 开始
        .length    = 16,
        .factor    = 0.1,
        .offset    = 0,
        .name      = "engine_temp"
    }
};
```

信号在 CAN 数据帧中的布局：

```
data[0..7] 共 64 bits

  bit 0 ────── engine_speed (16 bits, factor=0.1)
  bit 16 ───── vehicle_speed (16 bits, factor=0.1)
  bit 32 ───── engine_temp (16 bits, factor=0.1)
  bit 48 ───── 未使用

对应 bytes:
  data[0] data[1] → engine_speed
  data[2] data[3] → vehicle_speed
  data[4] data[5] → engine_temp
```

`extract_bits()` 逐 bit 提取的细节：

**文件**：`bsw/can/can_if/src/can_signal.c` 第 5-21 行

```c
static unsigned int extract_bits(const struct can_frame *frame,
                                 int start_bit, int length)
{
    unsigned int value = 0;
    for (int i = 0; i < length; i++) {
        int bit_index  = start_bit + i;         // 全局 bit 索引
        int byte_index = bit_index / 8;          // 所在字节
        int bit_in_byte = bit_index % 8;         // 字节内偏移
        if (frame->data[byte_index] & (1 << bit_in_byte))
            value |= (1 << i);
    }
    return value;
}
```

**实例计算**：`cansend vcan0 123#AABBCCDDEEFF0011`

```
.data[0] = 0xAA = 10101010
.data[1] = 0xBB = 10111011
.data[2] = 0xCC = 11001100
.data[3] = 0xDD = 11011101
.data[4] = 0xEE = 11101110
.data[5] = 0xFF = 11111111

engine_speed: start_bit=0, length=16
  → 取 data[0] 的 bit 0-7 和 data[1] 的 bit 0-7
  → raw = (0xAA << 8) | 0xBB = 0xAABB = 43707
  → value = 43707 * 0.1 + 0 = 4370.7 rpm

vehicle_speed: start_bit=16, length=16
  → 取 data[2] 的 bit 0-7 和 data[3] 的 bit 0-7
  → raw = 0xCCDD = 52445
  → value = 52445 * 0.1 + 0 = 5244.5 km/h

engine_temp: start_bit=32, length=16
  → 取 data[4] 的 bit 0-7 和 data[5] 的 bit 0-7
  → raw = 0xEEFF = 61183
  → value = 61183 * 0.1 + 0 = 6118.3 °C
```

---

### 第 10 步：全局数据更新

`test_message_handler` 解码后将物理值写入全局变量：

**文件**：`rte/include/rte_vehicle_data.h` 第 4-14 行

```c
typedef struct {
    float engine_speed;      // rpm
    float vehicle_speed;     // km/h
    float engine_temp;       // °C
} rte_vehicle_data_t;

extern rte_vehicle_data_t g_vehicle_data;    // 全局可见
```

**文件**：`rte/src/rte_vehicle_data.c`（仅 2 行）

```c
#include "rte_vehicle_data.h"
rte_vehicle_data_t g_vehicle_data;
```

之后任何代码都可以通过 `Rte_IRead_EngineSpeed()` 读取：

```c
// rte/src/rte.c 第 54-57 行
float Rte_IRead_EngineSpeed(void)
{
    return g_vehicle_data.engine_speed;
}
```

---

### 完整流程图

```
cansend vcan0 123#AABBCCDDEEFF0011
       │
       ▼ Linux 内核 SocketCAN 驱动
       │  写入内核缓冲区
       ▼
main.c L158: Rte_Run()
       │
       ▼
rte.c L49: can_router_poll()
       │
       ├── L35: can_scheduler_run(g_fd)         检查周期报文
       │
       └── L37: can_recv(g_fd, &rx_frame)
       │       │
       │       ▼ can_driver.c L29: read(fd, frame, sizeof(*frame))
       │       │  Linux 系统调用，从内核取一帧
       │       │  rx_frame = { can_id=0x123, data={0xAA,0xBB,...} }
       │       │
       │       ▼
       ├── L39: can_filter_match(0x123)
       │       │  filter_ids[] = {0x100, 0x200, 0x123}
       │       │  匹配 → return 1
       │       │
       │       ▼
       ├── L41: can_dispatch(&rx_frame)         ═══ 路径 A ═══
       │       │
       │       ▼ dispatcher.c L35: handlers[i].cb(&rx_frame)
       │       │  调用 rte_internal_bsw_adapter
       │       │
       │       ▼ rte.c L93: 遍历 swc_callbacks[]
       │       │  找到 can_id=0x123 → 调 callback
       │       │
       │       ▼ main.c L52: debug_handler_callback(0x123, data, 8)
       │           printf("[SWC] DEBUG: CAN ID 0x123, len=8\n")
       │           for i in 0..7: printf("data[%d] = 0x%02X\n")
       │
       └── L43: can_message_process(&rx_frame)  ═══ 路径 B ═══
               │
               ▼ can_message.c L11: find_message_desc(0x123)
               │  找到 {name="TEST_STATUS", handler=test_message_handler}
               │
               ▼ L21: test_message_handler(&rx_frame)
                   │
                   ▼ can_message_handler.c L12: can_decode(&rx_frame, &sig)
                   │     │
                   │     ▼ can_signal.c L31: can_db[i].can_id == 0x123 → 匹配
                   │     │     engine_speed: extract_bits(frame, 0, 16) → 0xAABB → 43707 * 0.1 = 4370.7
                   │     │     vehicle_speed: extract_bits(frame, 16, 16) → 0xCCDD → 52445 * 0.1 = 5244.5
                   │     │     engine_temp: extract_bits(frame, 32, 16) → 0xEEFF → 61183 * 0.1 = 6118.3
                   │     │
                   ▼ L14-18: 写入全局变量
                       g_vehicle_data.engine_speed = 4370.7
                       g_vehicle_data.vehicle_speed = 5244.5
                       g_vehicle_data.engine_temp = 6118.3
                       printf("[SIGNAL] engine_speed=4370.70 rpm")
                       printf("[SIGNAL] vehicle_speed=5244.50 km/h")
                       printf("[SIGNAL] engine_temp=6118.30 C")

       │
       ▼ main.c L161: isotp_receive(...)    检查诊断报文
       │  不是诊断报文 → return 0
       │
       ▼ main.c L172: doip_poll(&doip, ...) 检查以太网
       │  无连接 → return
       │
       ▼ main.c L174: usleep(1000)          等 1ms
       │
       ▼ while(1) 从头开始
```

---

## 三、链路二：UDS 诊断请求（DoIP 通道）

> **场景**：另一台电脑通过 TCP 连接端口 13400，发 DoIP 协议格式的 UDS 请求 `22 F1 00`（读发动机转速）。

### 第 1 步：doip_poll 检测 TCP 数据

**文件**：`bsw/tcp_ip/doip/src/doip.c` 第 172-198 行

```c
void doip_poll(doip_handle_t *handle,
                int (*uds_callback)(const uint8_t *, uint16_t,
                                    uint8_t *, uint16_t *))
{
    fd_set read_fds;
    FD_ZERO(&read_fds);

    int max_fd = 0;

    // 关注三个 socket：UDP（车辆发现）、TCP listen（新连接）、TCP diagnostic（诊断消息）
    if (handle->udp_fd >= 0)          FD_SET(handle->udp_fd, &read_fds);
    if (handle->tcp_fd >= 0)          FD_SET(handle->tcp_fd, &read_fds);
    if (handle->diagnostic_fd >= 0)    FD_SET(handle->diagnostic_fd, &read_fds);

    struct timeval tv = {0, 0};        // 非阻塞 select
    int ret = select(max_fd + 1, &read_fds, NULL, NULL, &tv);

    if (ret <= 0)
        return;                        // 无事件
```

`select` 是 Linux 的 I/O 多路复用系统调用。`{0,0}` 超时 = 立即返回，不阻塞。

### 第 2 步：DoIP 协议解析

**文件**：`bsw/tcp_ip/doip/src/doip.c` 第 244-381 行

```c
    // 诊断 socket 有数据
    if (handle->diagnostic_fd >= 0 &&
        FD_ISSET(handle->diagnostic_fd, &read_fds)) {

        uint8_t tcp_buf[4096];
        int n = (int)read(handle->diagnostic_fd, tcp_buf, sizeof(tcp_buf));

        // 解析 DoIP 头（固定 8 字节）
        uint8_t  proto_ver    = tcp_buf[0];          // 协议版本
        uint8_t  inv_ver      = tcp_buf[1];          // 反码版本
        uint16_t payload_type = read16(&tcp_buf[2]); // 负载类型
        uint32_t payload_len  = read32(&tcp_buf[4]); // 负载长度

        const uint8_t *payload = &tcp_buf[8];         // 负载从这里开始

        switch (payload_type) {

        case DOIP_DIAGNOSTIC_MESSAGE:                   // 0x8001
            // 从 payload 中提取源地址、目标地址和 UDS 数据
            uint16_t src_addr = read16(&payload[0]);   // 源逻辑地址
            uint16_t tgt_addr = read16(&payload[2]);   // 目标逻辑地址
            uint16_t uds_len  = payload_len - 4;        // UDS 数据长度
            const uint8_t *uds_data = &payload[4];      // UDS 数据开始
```

DoIP 诊断消息的 TCP 载荷格式：

```
┌─────────┬─────────┬────────────┬────────────┐
│ 字节 0-1 │ 字节 2-3 │  字节 4 起  │             │
│ 源地址   │ 目标地址 │ UDS 数据   │             │
│ (2B)     │ (2B)     │ (N 字节)   │             │
└─────────┴─────────┴────────────┴────────────┘
```

所以如果你发：
```
TCP payload: 00 00 00 0E 80  22 F1 00
                │     │       └── UDS: 0x22 读, DID=0xF100
                │     └── 目标地址 0x0E80
                └── 源地址 0x0000
```

则解析结果：`uds_data=&payload[4]` = `{0x22, 0xF1, 0x00}`, `uds_len=3`

### 第 3 步：UDS 引擎 dispatch

```c
            // doip.c 第 343-370 行
            if (uds_callback) {
                uint8_t uds_resp[4095];
                uint16_t uds_resp_len = 0;

                if (uds_callback(uds_data, uds_len,    // ← 调 main.c 注册的回调
                                  uds_resp, &uds_resp_len) == 0) {
                    // 构建诊断消息确认，发回给客户端
                    ...
                    send_doip_message(handle->diagnostic_fd,
                                       DOIP_DIAGNOSTIC_MESSAGE_ACK,
                                       diag_ack, ack_len);
                }
            }
```

`uds_callback` 是在 `main.c` 第 144 行传进来的 `doip_uds_handler`：

**文件**：`app/can_service/src/main.c` 第 102-111 行

```c
static int doip_uds_handler(const uint8_t *req, uint16_t req_len,
                             uint8_t *resp, uint16_t *resp_len)
{
    uds_response_t response;
    uds_handle_request(req, req_len, &response);       // ← 第 106 行

    memcpy(resp, response.data, response.len);
    *resp_len = response.len;
    return 0;
}
```

### 第 4 步：handle_read_data 内部

**文件**：`bsw/diag/uds/src/uds_core.c` 第 509-532 行

```c
int uds_handle_request(const uint8_t *request, uint16_t req_len,
                        uds_response_t *response)
{
    uint8_t sid = request[0];                           // req[0] = 0x22

    // 从 uds_services[] 表查找
    for (size_t i = 0; i < UDS_SERVICE_COUNT; i++) {
        if (uds_services[i].sid == sid) {               // 找到 sid=0x22
            uds_services[i].handler(request, req_len, response);
            return 0;
        }
    }

    // 没找到 → 回复 7F 22 11
    build_negative_response(sid, UDS_NRC_SERVICE_NOT_SUPPORTED, response);
    return 0;
}
```

`uds_services[]` 表（第 483-490 行）：

```c
static const uds_service_t uds_services[] = {
    {UDS_SID_DIAG_SESSION_CONTROL, 0x10, handle_session_control},
    {UDS_SID_READ_DATA_BY_ID,      0x22, handle_read_data},       ← 匹配！
    {UDS_SID_WRITE_DATA_BY_ID,     0x2E, handle_write_data},
    {UDS_SID_SECURITY_ACCESS,      0x27, handle_security_access},
    {UDS_SID_READ_DTC_INFO,        0x19, handle_read_dtc},
    {UDS_SID_CLEAR_DTC,            0x14, handle_clear_dtc},
};
```

跳转到 `handle_read_data()`（第 140-226 行）：

```c
static void handle_read_data(const uint8_t *req, uint16_t req_len,
                              uds_response_t *resp)
{
    uint16_t did = ((uint16_t)req[1] << 8) | req[2];  // 0xF100

    uint8_t data[256];
    uint16_t data_len = 0;

    switch (did) {

    case UDS_DID_ENGINE_SPEED:    // 0xF100
        if (g_read_cb && g_read_cb(did, data, sizeof(data)) >= 0) {
            data_len = 4;         // 外部回调提供了数据
        } else {
            float speed = 0.0f;   // 默认值
            memcpy(data, &speed, sizeof(speed));
            data_len = sizeof(speed);
        }
        break;

    // ... 其他 DID ...

    default:
        // 尝试外部回调
        if (g_read_cb && g_read_cb(did, data, sizeof(data)) >= 0) {
            data_len = 4;
        } else {
            // 不支持的 DID
            build_negative_response(UDS_SID_READ_DATA_BY_ID,
                                     UDS_NRC_REQUEST_OUT_OF_RANGE, resp);
            return;
        }
        break;
    }

    // 构建肯定响应: [0x62] [0xF1] [0x00] [4字节float]
    uint8_t resp_data[256];
    resp_data[0] = (did >> 8) & 0xFF;  // 0xF1
    resp_data[1] = did & 0xFF;         // 0x00
    memcpy(&resp_data[2], data, data_len);

    build_positive_response(UDS_SID_READ_DATA_BY_ID, resp_data, data_len + 2, resp);
}
```

### 第 5 步：读取实时车辆数据

`g_read_cb` 是在 `main.c` 第 139 行注册的：

```c
uds_register_read_callback(read_vehicle_data);
```

**文件**：`app/can_service/src/main.c` 第 62-85 行

```c
static int read_vehicle_data(uint16_t did, uint8_t *data, uint16_t max_len)
{
    (void)max_len;

    switch (did) {
    case UDS_DID_ENGINE_SPEED: {
        float val = Rte_IRead_EngineSpeed();         // ← 通过 RTE 读
        memcpy(data, &val, sizeof(val));
        return sizeof(val);
    }
    case UDS_DID_VEHICLE_SPEED: {
        float val = Rte_IRead_VehicleSpeed();
        memcpy(data, &val, sizeof(val));
        return sizeof(val);
    }
    case UDS_DID_ENGINE_TEMP: {
        float val = Rte_IRead_EngineTemp();
        memcpy(data, &val, sizeof(val));
        return sizeof(val);
    }
    default:
        return -1;                                     // 不支持
    }
}
```

`Rte_IRead_EngineSpeed()` 最终返回 `g_vehicle_data.engine_speed`——就是链路一中 `test_message_handler` 解码写入的那个值。

### 第 6 步：响应回传

```
UDS 引擎生成响应:
  resp.data = [0x62] [0xF1] [0x00] [4字节 float 2109.0]
  resp.len = 1 + 2 + 4 = 7

        │
        ▼ 回到 doip_uds_handler (main.c L106)
    uds_handle_request 返回后，response 已填好
    memcpy(resp, response.data, response.len)  // 拷贝到 doip 的响应缓冲区

        │
        ▼ 回到 doip.c L366
    构建诊断消息确认 (DoIP Diagnostic Message Acknowledgment):
      diag_ack[0..1] = tgt_addr (0x0E80)
      diag_ack[2..3] = src_addr (0x0000)
      diag_ack[4]    = 0        (ack code: 成功)
      diag_ack[5]    = 0        (预留)
      diag_ack[6..]  = UDS 响应 (62 F1 00 ...)

        │
        ▼ send_doip_message(fd, DOIP_DIAGNOSTIC_MESSAGE_ACK, diag_ack, ack_len)
    TCP 发送:
      [DoIP 头 8 字节] [diag_ack 数据]
        │
        ▼ 诊断仪收到并解析
```

**诊断仪收到的完整 TCP 数据**：
```
DoIP 头 (8 字节):
  [版本=0x03] [反码=0xFC] [类型=0x8002 (诊断ACK)] [长度=N]

诊断 ACK 载荷:
  [源地址 0x0E80] [目标地址 0x0000] [ACK=0x00] [预留=0x00]
  [UDS 响应 62 F1 00 00 00 04 4F]
  ↑ 肯定响应     ↑ 发动机转速 float 值
```

---

## 四、链路三：ISO-TP 多帧传输

### 4.1 为什么需要 ISO-TP

CAN 2.0 一帧最多 8 字节数据。但以下场景都需要超过 8 字节：

| 场景 | 数据量 | CAN 直接发 |
|------|--------|-----------|
| 读 VIN (0x22 F190) | 17 字节 | ❌ 一帧装不下 |
| 写 VIN (0x2E F190) | 17+ 字节 | ❌ |
| 固件升级 | 几百 KB ~ 几 MB | ❌ |
| 读多个 DID | 可能 20+ 字节 | ❌ |

ISO-TP（ISO 15765-2）就是解决这个问题的：**把长报文拆成多帧，接收端重组**。

### 4.2 发送多帧的全过程

假设要发送 20 字节数据到 CAN ID 0x7E8。

**第 1 步：调用 isotp_send()**

**文件**：`bsw/diag/iso_tp/src/iso_tp.c` 第 168-208 行

```c
int isotp_send(uint32_t can_id, const uint8_t *data, uint32_t len)
{
    // 参数校验
    if (!data || len == 0 || len > ISO_TP_MAX_LEN) return -1;

    // 检查状态机是否空闲
    if (tx_state != ISO_TP_TX_IDLE) return -2;   // 忙

    // 保存到发送缓冲区
    tx_id = can_id;                     // 0x7E8
    tx_total_len = len;                 // 20
    memcpy(tx_buffer, data, len);

    if (len <= 7) {
        // 单帧模式 — 一帧发完（本例不走这里，因为 20 > 7）
        build_sf(&frame, can_id, data, len);
        can_send(g_fd, &frame);
        tx_state = ISO_TP_TX_IDLE;
    } else {
        // 多帧模式 — 先发首帧
        build_ff(&frame, can_id, data, len);       // → 构建首帧
        can_send(g_fd, &frame);                     // → 发送

        tx_sent = 6;           // 首帧带了前 6 字节
        tx_seq  = 1;           // 后续 CF 序号从 1 开始
        tx_state = ISO_TP_TX_WAIT_FC;   // → 等待对方发流控帧
        tx_timeout_ms = get_time_ms() + 1000;
    }
}
```

**首帧的 CAN 数据布局**：

```
CAN ID: 0x7E8
CAN data:
  [0] = 0x10 | ((20 >> 8) & 0x0F) = 0x10 | 0x01 = 0x11    PCI: 首帧 + 长度高4位
  [1] = 20 & 0xFF = 0x14                                      长度低8位
  [2..7] = data[0..5] (前 6 字节)
```

函数 `build_ff()`：

**文件**：`bsw/diag/iso_tp/src/iso_tp.c` 第 86-96 行

```c
static void build_ff(struct can_frame *frame, uint32_t can_id,
                      const uint8_t *data, uint32_t len)
{
    memset(frame, 0, sizeof(*frame));
    frame->can_id = can_id;
    frame->can_dlc = 8;                           // 首帧 + 6 字节数据 = 2 + 6

    frame->data[0] = ISO_TP_PCI_FF | ((len >> 8) & 0x0F);   // 0x10 | 长度高位
    frame->data[1] = len & 0xFF;                             // 长度低位
    memcpy(&frame->data[2], data, ISO_TP_FF_DATA_LEN);       // 前 6 字节
}
```

**第 2 步：等待 FC（流控帧）**

发送方状态变成 `ISO_TP_TX_WAIT_FC`，不再发送新数据。每次 `isotp_poll()` 被调用时，检查超时和接收 FC。

**第 3 步：接收方发回 FC**

接收方（用另一个进程模拟）收到 FF 后，回复 FC：

```
CAN ID: 0x7E0  (发送方的 ID)
CAN data:
  [0] = 0x30 | ISO_TP_FC_CTS = 0x30       PCI: 流控帧 + Continue To Send
  [1] = 0x00                                BS (块大小: 0 = 不限)
  [2] = 10                                  ST (最小间隔: 10ms)
```

函数 `build_fc()`：

**文件**：`bsw/diag/iso_tp/src/iso_tp.c` 第 102-112 行

```c
static void build_fc(struct can_frame *frame, uint32_t can_id,
                      uint8_t fc_status, uint8_t bs, uint8_t st)
{
    memset(frame, 0, sizeof(*frame));
    frame->can_id = can_id;
    frame->can_dlc = 3;                         // FC 固定 3 字节

    frame->data[0] = ISO_TP_PCI_FC | (fc_status & 0x0F);  // 0x30 | CTS=0
    frame->data[1] = bs;                         // 块大小
    frame->data[2] = st;                         // 最小间隔
}
```

**第 4 步：发送方收到 FC，继续发 CF**

**文件**：`bsw/diag/iso_tp/src/iso_tp.c` 第 366-412 行

```c
case ISO_TP_PCI_FC: {
    if (tx_state != ISO_TP_TX_WAIT_FC)
        break;

    uint8_t fc_status = frame.data[0] & 0x0F;

    if (fc_status != ISO_TP_FC_CTS) {
        tx_state = ISO_TP_TX_IDLE;                // 对方说不让发了
        break;
    }

    // 收到 CTS，开始发连续帧
    tx_state = ISO_TP_TX_SENDING_CF;

    // 立即发第一个 CF
    struct can_frame cf_frame;
    uint32_t remaining = tx_total_len - tx_sent;   // 20 - 6 = 14
    uint32_t cf_len = (remaining > 7) ? 7 : remaining;  // min(14, 7) = 7

    build_cf(&cf_frame, tx_id, tx_seq,             // seq = 1
              tx_buffer + tx_sent, cf_len);        // 从 data[6] 开始发 7 字节
    can_send(g_fd, &cf_frame);

    tx_sent += cf_len;                             // 6 + 7 = 13

    if (tx_sent >= tx_total_len) {
        tx_state = ISO_TP_TX_IDLE;                 // 发完了（但还有 7 字节没发？）
    } else {
        tx_seq = (tx_seq + 1) & 0x0F;              // seq = 2
        // 状态仍是 SENDING_CF，下一次 poll 继续发
    }
}
```

仔细看：这里 FC 处理中**只发了一个 CF**，然后状态回到 `ISO_TP_TX_SENDING_CF`。下一个循环 `isotp_poll()` 会再次检查，发现状态是 `SENDING_CF`，但它没有相应的处理！——这是实现中的一个问题。

正确的做法应该是：在 FC 处理中一次性发完所有 CF，或者在 `SENDING_CF` 状态下也处理发送。

当前代码中 FC 的 fall-through（第 382 行注释说"fall through"）并没有真正实现——它只发了一个 CF 就 break 了。后续的 CF 不会被发送，因为 `isotp_poll()` 中只有 `ISO_TP_PCI_FC` 分支驱动 CF 的发送。

**这是当前 ISO-TP 实现的一个 BUG**：发送多帧时，只有第一个 CF 会被发送。

### 4.3 接收多帧的全过程

**文件**：`bsw/diag/iso_tp/src/iso_tp.c` 第 243-363 行

```
接收方状态机:

  IDLE ──收到 FF──→ WAITING_CF ──收到 CF──→ 拼接收

                                  如果收完 → IDLE (数据就绪)

                                  超时 → IDLE (丢弃)
```

**收到 SF（单帧）时**：

```c
case ISO_TP_PCI_SF: {
    uint32_t sf_len = get_frame_length(&frame);    // 从 data[0] & 0x0F 取长度
    if (sf_len > 7) break;

    rx_id = frame.can_id;
    memcpy(rx_buffer, &frame.data[1], sf_len);     // 数据从 data[1] 开始
    rx_total_len = sf_len;                          // 标记有完整报文

    printf("[ISO_TP] RX SF: id=0x%X, len=%lu\n", frame.can_id, sf_len);
    break;
}
```

**收到 FF（首帧）时**：

```c
case ISO_TP_PCI_FF: {
    uint32_t ff_len = get_frame_length(&frame);    // 读取总长度
    // ...
    rx_id = frame.can_id;
    rx_total_len = ff_len;

    memcpy(rx_buffer, &frame.data[2], 6);           // 前 6 字节数据
    rx_received = 6;
    rx_exp_seq = 1;                                  // 期待 CF 序号=1
    rx_state = ISO_TP_RX_WAITING_CF;

    // 发送流控帧
    build_fc(&fc_frame, frame.can_id, ISO_TP_FC_CTS, 0, 10);
    can_send(g_fd, &fc_frame);

    rx_timeout_ms = get_time_ms() + 1000;
    break;
}
```

**收到 CF（连续帧）时**：

```c
case ISO_TP_PCI_CF: {
    if (rx_state != ISO_TP_RX_WAITING_CF) break;    // 不在接收状态

    uint8_t seq = frame.data[0] & 0x0F;

    if (seq != rx_exp_seq) {                         // 序号不匹配
        rx_state = ISO_TP_RX_IDLE;
        rx_total_len = 0;                            // 丢弃
        break;
    }

    uint32_t cf_len = frame.can_dlc - 1;             // 本帧数据长度（去掉 PCI）
    uint32_t remaining = rx_total_len - rx_received; // 还差多少
    if (cf_len > remaining) cf_len = remaining;

    memcpy(rx_buffer + rx_received, &frame.data[1], cf_len);  // 拼接到缓冲区
    rx_received += cf_len;

    rx_exp_seq = (seq + 1) & 0x0F;                   // 期待下一序号

    if (rx_received >= rx_total_len) {
        printf("[ISO_TP] RX complete: id=0x%X, len=%lu\n", rx_id, rx_total_len);
        rx_state = ISO_TP_RX_IDLE;
        // rx_buffer 中有完整的报文，rx_total_len 是长度
        // 等待 isotp_receive() 取走
    }
    break;
}
```

**调用者取数据**：

```c
// iso_tp.c 第 210-241 行
int isotp_receive(uint32_t *can_id, uint8_t *buf, uint32_t buf_size)
{
    if (rx_state != ISO_TP_RX_IDLE) {
        return 0;              // 还没收完
    }

    if (rx_total_len == 0) {
        return 0;              // 没数据
    }

    if (buf_size < rx_total_len) {
        rx_total_len = 0;
        return -1;             // 缓冲区太小
    }

    if (can_id) *can_id = rx_id;
    memcpy(buf, rx_buffer, rx_total_len);    // 拷贝给调用者
    uint32_t received = rx_total_len;
    rx_total_len = 0;                         // 清除

    return (int)received;                     // 返回长度
}
```

---

## 五、回调系统详解（难点）

### 5.1 三层回调体系

AutoCore 的回调分为**三层**，这是让你困惑的核心原因：

```
第 1 层: BSW 层回调 (can_dispatcher.c)
─────────────────────────────────────────────
  数据结构: handlers[MAX_CAN_HANDLER]
  注册方式: can_register(can_id, callback)
  回调签名: void (*)(const struct can_frame *frame)
  设计目的: BSW 内部使用，直接传递 Linux 内核结构体

第 2 层: RTE 适配器回调 (rte.c)
─────────────────────────────────────────────
  数据结构: swc_callbacks[MAX_SWC_CALLBACKS]
  注册方式: Rte_Call_RegisterRxCallback(can_id, callback)
  回调签名: void (*)(uint32_t can_id, const uint8_t *data, uint8_t len)
  设计目的: 将 BSW 的 can_frame 转为简单的 (id, data, len)

第 3 层: 消息处理回调 (can_message_db.c)
─────────────────────────────────────────────
  数据结构: can_message_db[] 静态数组
  注册方式: 编译时在数组中定义
  回调签名: void (*)(const struct can_frame *frame)
  设计目的: 预设的"这个 CAN ID 应该怎么解析信号"
```

### 5.2 回调注册流程

启动时 main.c 第 126-128 行：

```c
Rte_Call_RegisterRxCallback(0x100, engine_status_callback);
Rte_Call_RegisterRxCallback(0x200, vehicle_status_callback);
Rte_Call_RegisterRxCallback(0x123, debug_handler_callback);
```

每个调用内部 (rte.c 第 101-123 行)：

```
Rte_Call_RegisterRxCallback(0x123, debug_handler_callback)
  │
  ├── ① 保存到 RTE 的 swc_callbacks[]:
  │     swc_callbacks[count].can_id   = 0x123
  │     swc_callbacks[count].callback = debug_handler_callback
  │     swc_callback_count++
  │
  └── ② 注册到 BSW 的 can_dispatcher:
        can_router_register(0x123, rte_internal_bsw_adapter)
          │
          └── can_register(0x123, rte_internal_bsw_adapter)
                handlers[count].can_id = 0x123
                handlers[count].cb     = rte_internal_bsw_adapter
                handler_count++

    ③ 加入过滤器:
        can_router_add_filter(0x123)
          └── can_filter_add(0x123)
                filter_ids[count] = 0x123
                filter_count++
```

### 5.3 为什么这样设计

```
为什么不用 can_dispatcher 直接调用应用层回调？

因为 can_dispatcher 传的是 struct can_frame *（Linux 内核结构体）
如果应用层直接使用它，就产生了对 Linux SocketCAN 的依赖
换到 FreeRTOS + MCP2515 时，struct can_frame 就不存在了

RTE 的适配器做了格式转换：
  struct can_frame * → (uint32_t can_id, const uint8_t *data, uint8_t len)
                                                      ↑
                                                      不依赖任何特定的 CAN 硬件实现


为什么又有 can_message_process + can_dispatch 两条路径？

历史原因：这是两条职责不同的路径
  - can_dispatch: "通知" — 让注册了回调的应用模块知道有报文来了
  - can_message_process: "解析" — 不管谁注册了回调，预设的解析逻辑都要执行

理想的架构应该合并：
  can_message_process 中的 test_message_handler 做的信号解码工作，
  其实应该通过 RTE 层暴露为服务，而不是硬编码在 can_message_db 中。
```

---

## 六、当前架构的问题

### 6.1 CAN 通道的 UDS 诊断不可用

这是最严重的问题。原因：

```
while (1) {
    Rte_Run();                    ← L158: can_router_poll() → can_recv() 消费了 CAN 帧
                                      ↓
                                      这里 ISO-TP 需要的 CAN 帧已经被读走了

    isotp_receive(&rx_id, buf);   ← L161: 检查是否有 ISO-TP 报文
                                      但 CAN 帧在 L158 已被消费
                                      isotp_poll() 从未被调用！
                                      结果是永远收不到 UDS 诊断请求

    doip_poll(&doip, handler);    ← L172: DoIP 用 TCP，不受影响
}
```

整理出表格：

| 诊断通道 | 驱动的函数 | CAN 帧来源 | 可用性 |
|----------|-----------|-----------|--------|
| CAN UDS | 需要 `isotp_poll()` | 需要 can_recv() | ❌ 不可用 — iso_tp 从未被驱动 |
| DoIP | `doip_poll()` | TCP socket | ✅ 正常 |

**修复方案**：在 main.c 的 while 循环中正确调度：

```c
while (1) {
    // 先驱动 ISO-TP（它会调用 can_recv）
    isotp_poll();

    // 再驱动普通的 CAN 通信
    Rte_Run();    // can_router_poll() 内部也会 can_recv，但已经被 isotp 消费了...
}
```

但这仍有问题——**同一个 CAN fd 被两个模块轮询，谁先读谁就消费了数据**。

**正确的 AUTOSAR 做法**：PDU Router（PDUR）统一管理报文路由。

```
CAN 帧 → can_recv() → PDUR → 判断报文类型:
                              ├── 诊断报文 (0x7E0/0x7E8) → ISO-TP
                              └── 普通报文 → CanIf (过滤器/分发/解码)
```

### 6.2 回调路径重复

对于 0x123 报文，它同时走两条路径：

```
can_router_poll()
  │
  ├── can_dispatch(&rx_frame)       → 调 debug_handler_callback
  │                                   打印 "DEBUG: CAN ID 0x123"
  │
  └── can_message_process(&rx_frame) → 调 test_message_handler
                                        解码信号 + 更新 g_vehicle_data
                                        打印 "engine_speed=..."
```

**问题**：对于 0x100 和 0x200 报文，只有 `can_dispatch` 路径有效（`can_message_db` 中它们的 handler 为 NULL）。对于 0x123，两条路径都执行——一条打印内容，一条解码信号，互不干扰但做了重复的事。

**理想的设计**：统一用 `can_dispatch` / RTE 层做所有消息的分发，不需要 `can_message_process` 这条独立路径。

---

## 附录：文件行号速查表

| 文件 | 关键行号 | 内容 |
|------|---------|------|
| `main.c` | 117-178 | main() 全部流程 |
| `main.c` | 126-128 | 注册三个 CAN 回调 |
| `main.c` | 138-140 | 注册 UDS 读写回调 |
| `main.c` | 143-148 | DoIP 初始化 |
| `main.c` | 157-175 | while(1) 主循环 |
| `rte.h` | 45, 55, 62-72 | Rte_Init, Rte_Run, Rte_IRead_* 声明 |
| `rte.c` | 22-41 | Rte_Init 实现 |
| `rte.c` | 44-49 | Rte_Run 实现 |
| `rte.c` | 54-67 | Rte_IRead_* 实现（读全局变量） |
| `rte.c` | 90-98 | rte_internal_bsw_adapter（RTE 回调适配器） |
| `rte.c` | 101-123 | Rte_Call_RegisterRxCallback（注册应用回调） |
| `rte.c` | 125-138 | Rte_Call_SendCanFrame |
| `can_driver.c` | 49-80 | can_open — socket + ioctl + bind |
| `can_driver.c` | 16-23 | can_send — write() |
| `can_driver.c` | 28-42 | can_recv — read() |
| `can_router.c` | 13-15 | can_router_init |
| `can_router.c` | 33-45 | can_router_poll — 核心枢纽 |
| `can_dispatcher.c` | 28-37 | can_dispatch — 调回调 |
| `can_filter.c` | 14-21 | can_filter_add — 添加过滤 ID |
| `can_filter.c` | 24-32 | can_filter_match — 检查 ID 是否匹配 |
| `can_message.c` | 6-21 | can_message_process — 消息分类处理 |
| `can_message_handler.c` | 9-27 | test_message_handler — 调用 can_decode + 更新全局数据 |
| `can_signal.c` | 5-21 | extract_bits — 逐 bit 提取 |
| `can_signal.c` | 26-52 | can_decode — 提取所有信号 |
| `can_db.c` | 3-28 | can_db[] — 信号定义表 |
| `can_message_db.c` | 5-24 | can_message_db[] — 消息 ID → 处理函数映射 |
| `iso_tp.c` | 168-208 | isotp_send — 发送（SF 或 FF） |
| `iso_tp.c` | 210-241 | isotp_receive — 取完整报文 |
| `iso_tp.c` | 243-419 | isotp_poll — 驱动状态机（收发） |
| `uds.h` | 40-99 | SID/DID/NRC 常量定义 |
| `uds_core.c` | 93-132 | handle_session_control — 0x10 |
| `uds_core.c` | 140-226 | handle_read_data — 0x22 |
| `uds_core.c` | 234-287 | handle_write_data — 0x2E |
| `uds_core.c` | 296-384 | handle_security_access — 0x27 |
| `uds_core.c` | 393-448 | handle_read_dtc — 0x19 |
| `uds_core.c` | 456-473 | handle_clear_dtc — 0x14 |
| `uds_core.c` | 477-490 | uds_services[] — SID 分发表 |
| `uds_core.c` | 509-532 | uds_handle_request — 入口 |
| `doip.c` | 100-170 | doip_init — UDP + TCP 初始化 |
| `doip.c` | 172-382 | doip_poll — select + 协议处理 |
| `doip.c` | 293-321 | 路由激活处理 |
| `doip.c` | 324-371 | 诊断消息处理 |
