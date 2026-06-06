/**
 * @file main.c
 * @brief AutoCore Gateway — Application Main
 *
 * 整合所有应用 SWC 的主入口：
 *   1. CAN Service SWC  — CAN 报文收发和信号处理
 *   2. UDS Service SWC  — 诊断服务 (ISO 14229)
 *
 * 所有模块通过 RTE 接口通信，不直接操作 BSW。
 */

#include "rte.h"
#include "uds.h"
#include "iso_tp.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* ==================== CAN Service SWC ==================== */

/** 发动机状态回调 */
static void engine_status_callback(uint32_t can_id,
                                    const uint8_t *data,
                                    uint8_t len)
{
    (void)data;
    (void)len;
    printf("[SWC] ENGINE_STATUS received (CAN ID 0x%X)\n", can_id);

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

/* ==================== UDS Service SWC ==================== */

/** 外部 DID 读取回调 — 从 RTE 获取实时数据 */
static int read_vehicle_data(uint16_t did, uint8_t *data, uint16_t max_len)
{
    (void)max_len;

    switch (did) {
    case UDS_DID_ENGINE_SPEED: {
        float val = Rte_IRead_EngineSpeed();
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
        return -1;
    }
}

/** 外部 DID 写入回调 */
static int write_vehicle_data(uint16_t did, const uint8_t *data, uint16_t len)
{
    (void)data;
    (void)len;
    printf("[UDS_SWC] Write DID 0x%04X (%u bytes) - logged\n", did, len);
    return 0;
}

/* ==================== 主函数 ==================== */

int main(void)
{
    printf("===== AutoCore Gateway =====\n\n");

    /* ---- Step 1: 初始化 RTE（打开 CAN 设备 + 初始化路由器） ---- */
    if (Rte_Init() < 0) {
        printf("[APP] ERROR: RTE initialization failed\n");
        return -1;
    }

    /* ---- Step 2: 注册 CAN Service SWC 回调 ---- */
    Rte_Call_RegisterRxCallback(0x100, engine_status_callback);
    Rte_Call_RegisterRxCallback(0x200, vehicle_status_callback);
    Rte_Call_RegisterRxCallback(0x123, debug_handler_callback);

    /* ---- Step 3: 注册周期性发送 ---- */
    uint8_t heartbeat_data[] = {0x01};
    Rte_Call_SendCanFrame(0x700, heartbeat_data, 1);

    uint8_t tx_data[] = {0xAA, 0x55};
    Rte_Call_SendCanFrame(0x555, tx_data, 2);

    /* ---- Step 4: 初始化 UDS ---- */
    uds_init(0x7E0, 0x7E8);       /* 标准 UDS 寻址 */

    /* 注册外部 DID 回调，实现实时数据读取 */
    uds_register_read_callback(read_vehicle_data);
    uds_register_write_callback(write_vehicle_data);

    printf("[APP] All SWCs initialized, entering main loop...\n\n");

    /* ---- Step 5: 主循环 ---- */
    uint8_t uds_request_buf[4095];
    uint32_t rx_id;

    while (1) {
        /* 驱动 RTE（底层 = can_router_poll + 调度器） */
        Rte_Run();

        /* 检查 ISO-TP 是否有完整的诊断请求报文 */
        int len = isotp_receive(&rx_id, uds_request_buf,
                                 sizeof(uds_request_buf));
        if (len > 0) {
            printf("[APP] UDS request received via ISO-TP\n");

            uds_response_t response;
            uds_handle_request(uds_request_buf, (uint16_t)len, &response);

            /* 通过 ISO-TP 发送响应 */
            isotp_send(uds_get_response_id(), response.data, response.len);
        }

        usleep(1000);
    }

    return 0;
}
