/**
 * @file main.c
 * @brief CAN Service — Application SWC
 *
 * 符合 AUTOSAR 分层架构的应用层示例。
 * 只通过 RTE 接口与底层通信，不直接引用任何 BSW 模块。
 */

#include "rte.h"
#include <stdio.h>
#include <unistd.h>

/* ==================== SWC 回调函数 ==================== */

/** 发动机状态回调 */
static void engine_status_callback(uint32_t can_id,
                                    const uint8_t *data,
                                    uint8_t len)
{
    (void)data;
    (void)len;
    printf("[SWC] ENGINE_STATUS received (CAN ID 0x%X)\n", can_id);

    /* 通过 RTE 读取最新的车辆数据 */
    printf("[SWC]   engine_speed = %.2f rpm\n",   Rte_IRead_EngineSpeed());
    printf("[SWC]   vehicle_speed = %.2f km/h\n", Rte_IRead_VehicleSpeed());
    printf("[SWC]   engine_temp = %.2f °C\n",     Rte_IRead_EngineTemp());
}

/** 车辆状态回调 */
static void vehicle_status_callback(uint32_t can_id,
                                     const uint8_t *data,
                                     uint8_t len)
{
    (void)data;
    (void)len;
    printf("[SWC] VEHICLE_STATUS received (CAN ID 0x%X)\n", can_id);
}

/** 通用调试回调 */
static void debug_handler_callback(uint32_t can_id,
                                    const uint8_t *data,
                                    uint8_t len)
{
    printf("[SWC] DEBUG: CAN ID 0x%X, len=%d\n", can_id, len);
    for (uint8_t i = 0; i < len && i < 8; i++) {
        printf("[SWC]   data[%d] = 0x%02X\n", i, data[i]);
    }
}

/* ==================== 主函数 ==================== */

int main(void)
{
    printf("===== AutoCore CAN Service =====\n\n");

    /* Step 1: 初始化 RTE（内部封装了 CAN 设备打开 + 路由器初始化） */
    if (Rte_Init() < 0) {
        printf("[APP] ERROR: RTE initialization failed\n");
        return -1;
    }

    /* Step 2: 通过 RTE 注册 SWC 回调 */
    Rte_Call_RegisterRxCallback(0x100, engine_status_callback);
    Rte_Call_RegisterRxCallback(0x200, vehicle_status_callback);
    Rte_Call_RegisterRxCallback(0x123, debug_handler_callback);

    /* Step 3: 通过 RTE 发送周期性 CAN 报文 */
    uint8_t heartbeat_data[] = {0x01};
    Rte_Call_SendCanFrame(0x700, heartbeat_data, 1);

    uint8_t tx_data[] = {0xAA, 0x55};
    Rte_Call_SendCanFrame(0x555, tx_data, 2);

    printf("[APP] CAN Service started, entering main loop...\n\n");

    /* Step 4: 主循环 — 应用层只调用 Rte_Run() */
    while (1) {
        Rte_Run();
        usleep(1000);  /* 1ms 轮询间隔 */
    }

    return 0;
}
