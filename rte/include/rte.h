#ifndef RTE_H
#define RTE_H

/**
 * @file rte.h
 * @brief RTE (Runtime Environment) — 应用层统一接口
 *
 * AUTOSAR RTE 是应用 SWC 与 BSW 之间的中间层。
 * 应用层代码只包含此头文件，不直接引用任何 BSW 模块。
 *
 * 命名约定（遵循 AUTOSAR 标准）：
 *   Rte_Init       — RTE 初始化
 *   Rte_Run        — RTE 主循环处理
 *   Rte_IRead_*    — 读取接口 (Interface Read)
 *   Rte_IWrite_*   — 写入接口 (Interface Write)
 *   Rte_Call_*     — 服务调用 (Client-Server)
 *
 * 使用示例：
 * @code
 *   #include "rte.h"
 *
 *   int main(void) {
 *       Rte_Init();
 *       while (1) {
 *           Rte_Run();
 *           float speed = Rte_IRead_VehicleSpeed();
 *           // ... 应用逻辑
 *       }
 *   }
 * @endcode
 */

#include <stdint.h>

/* ==================== 系统接口 ==================== */

/**
 * @brief RTE 及底层 BSW 初始化
 *
 * 封装了 MCAL CAN 驱动打开、CAN 接口层初始化等操作。
 * 应用层无需关心底层使用什么硬件接口。
 *
 * @return 0 成功, -1 失败
 */
int Rte_Init(void);

/**
 * @brief RTE 主循环处理
 *
 * 每帧调用一次，内部完成：
 * 1. 调度周期性发送 CAN 报文
 * 2. 接收并分发 CAN 报文到注册的 SWC 回调
 * 应用层无需理解 poll/dispatch 等底层细节。
 */
void Rte_Run(void);

/* ==================== 读接口 (Sender-Receiver) ==================== */

/**
 * @brief 读取发动机转速 (rpm)
 */
float Rte_IRead_EngineSpeed(void);

/**
 * @brief 读取车速 (km/h)
 */
float Rte_IRead_VehicleSpeed(void);

/**
 * @brief 读取发动机温度 (°C)
 */
float Rte_IRead_EngineTemp(void);

/* ==================== 写接口 (Sender-Receiver) ==================== */

/**
 * @brief 注册 CAN 接收回调（应用层 SWC 使用）
 *
 * AUTOSAR 中 SWC 通过 RTE 接收数据，典型方式是 RTE 收到数据后
 * 调用 SWC 的回调函数（Runable Entity）。
 *
 * @param can_id   CAN ID
 * @param callback 回调函数，收到指定 ID 的报文时被调用
 */
void Rte_Call_RegisterRxCallback(int can_id, void (*callback)(uint32_t can_id,
                                                               const uint8_t *data,
                                                               uint8_t len));

/**
 * @brief 发送 CAN 报文（应用层 SWC 使用）
 *
 * @param can_id CAN ID
 * @param data   数据缓冲区
 * @param len    数据长度 (≤ 8)
 * @return 0 成功, -1 失败
 */
int Rte_Call_SendCanFrame(uint32_t can_id, const uint8_t *data, uint8_t len);

#endif /* RTE_H */
