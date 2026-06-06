/**
 * @file uds_service.c
 * @brief UDS Service — Application SWC
 *
 * 通过 ISO-TP 传输层收发 UDS 诊断请求/响应。
 * 不直接操作 CAN 总线，完全通过 RTE + ISO-TP 接口通信。
 *
 * UDS 物理寻址（标准定义）：
 *   请求 ID: 0x7E0 (功能寻址可用 0x7DF)
 *   响应 ID: 请求 ID + 0x8 = 0x7E8
 */

#include "rte.h"
#include "uds.h"
#include "iso_tp.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* ==================== 内部状态 ==================== */

static int g_initialized = 0;

/* 请求和响应缓冲区 */
static uint8_t g_request_buf[4095];
static uint16_t g_request_len;

static uint8_t g_response_buf[4095];
static uint16_t g_response_len;

/* ==================== 外部 DID 回调实现 ==================== */

/**
 * 外部 DID 读取回调 — 从 RTE 获取实时车辆数据
 */
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
        return -1;  /* 不支持的 DID */
    }
}

/**
 * 外部 DID 写入回调 — 通过 RTE 写入车辆数据
 */
static int write_vehicle_data(uint16_t did, const uint8_t *data, uint16_t len)
{
    (void)data;
    (void)len;

    switch (did) {
    /* 目前仅日志记录，实际项目中通过 RTE 写入 */
    default:
        printf("[UDS_SWC] Write DID 0x%04X (%u bytes) - logged\n", did, len);
        return 0;
    }
}

/* ==================== 初始化 ==================== */

void uds_service_init(void)
{
    if (g_initialized)
        return;

    /* 初始化 ISO-TP 需要 CAN fd，这里假设 Rte_Init 已经创建了
     * 实际项目中 RTE 应提供获取 CAN fd 的接口 */
    printf("[UDS_SWC] Initializing...\n");

    /* ISO-TP 初始化需要在 CAN 打开之后，这里留到 poll 中延后初始化 */
    g_initialized = 1;

    printf("[UDS_SWC] Ready\n");
}

/* ==================== 主循环 ==================== */

void uds_service_run(void)
{
    static int iso_tp_inited = 0;

    if (!g_initialized)
        return;

    /* 延迟初始化 ISO-TP（需要等 CAN 打开） */
    // ISO-TP 的初始化需要 can_fd，目前通过 isotp_init 在单独的步骤完成
    // 或者通过 RTE 获取 fd

    /* 检查是否有完整的 ISO-TP 报文 */
    uint32_t rx_id;
    int len = isotp_receive(&rx_id, g_request_buf, sizeof(g_request_buf));

    if (len > 0) {
        printf("[UDS_SWC] Received ISO-TP message: id=0x%X, len=%d\n",
               rx_id, len);

        /* 处理 UDS 请求 */
        uds_response_t response;
        uds_handle_request(g_request_buf, (uint16_t)len, &response);

        /* 通过 ISO-TP 发送响应 */
        isotp_send(uds_get_response_id(), response.data, response.len);
    }
}
