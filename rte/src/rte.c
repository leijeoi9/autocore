#include "rte.h"
#include "rte_vehicle_data.h"

#include "can_driver.h"       /* MCAL: can_open */
#include "can_router.h"       /* CanIf: router 接口 */
#include "can_message.h"      /* CanIf: message 处理 */

#include <stdio.h>
#include <string.h>
#include <linux/can.h>

/* ==================== 内部状态 ==================== */

/** CAN socket 文件描述符 */
static int rte_can_fd = -1;

/** CAN 网络接口名称（可通过配置修改） */
static const char *rte_can_ifname = "vcan0";

/* ==================== 系统接口实现 ==================== */

int Rte_Init(void)
{
    /* Step 1: 打开 CAN 设备（MCAL 层） */
    rte_can_fd = can_open(rte_can_ifname);
    if (rte_can_fd < 0) {
        printf("[RTE] ERROR: can_open(%s) failed\n", rte_can_ifname);
        return -1;
    }
    printf("[RTE] CAN interface '%s' opened (fd=%d)\n",
           rte_can_ifname, rte_can_fd);

    /* Step 2: 初始化 CAN 路由器（CanIf 层） */
    can_router_init(rte_can_fd);

    printf("[RTE] RTE initialized successfully\n");
    return 0;
}

void Rte_Run(void)
{
    if (rte_can_fd < 0)
        return;

    /* 内部调用 BSW 的轮询接口 */
    can_router_poll();
}

/* ==================== 读接口实现 ==================== */

float Rte_IRead_EngineSpeed(void)
{
    return g_vehicle_data.engine_speed;
}

float Rte_IRead_VehicleSpeed(void)
{
    return g_vehicle_data.vehicle_speed;
}

float Rte_IRead_EngineTemp(void)
{
    return g_vehicle_data.engine_temp;
}

/* ==================== 写接口实现 ==================== */

/**
 * 简化的回调注册：将应用层的简单回调包装为 can_router_register 的调用
 *
 * 注意：这里用了一个简单的回调包装。AUTOSAR 标准中 RTE 需要更复杂的
 * 端口映射和数据类型转换，这里展示核心概念。
 */

/** 应用层注册的 SWC 回调列表 */
#define MAX_SWC_CALLBACKS 16

typedef struct {
    int can_id;
    void (*callback)(uint32_t can_id, const uint8_t *data, uint8_t len);
} swc_callback_t;

static swc_callback_t swc_callbacks[MAX_SWC_CALLBACKS];
static int swc_callback_count = 0;

/** 内部使用的 BSW 层回调适配器 */
static void rte_internal_bsw_adapter(const struct can_frame *frame)
{
    for (int i = 0; i < swc_callback_count; i++) {
        if (swc_callbacks[i].can_id == (int)frame->can_id) {
            swc_callbacks[i].callback(frame->can_id,
                                       frame->data,
                                       frame->can_dlc);
        }
    }
}

void Rte_Call_RegisterRxCallback(int can_id, void (*callback)(uint32_t,
                                                               const uint8_t *,
                                                               uint8_t))
{
    if (swc_callback_count >= MAX_SWC_CALLBACKS) {
        printf("[RTE] WARN: callback list full, cannot register 0x%X\n",
               can_id);
        return;
    }

    /* 保存应用层回调 */
    swc_callbacks[swc_callback_count].can_id = can_id;
    swc_callbacks[swc_callback_count].callback = callback;
    swc_callback_count++;

    /* 注册到 BSW 层（适配器模式） */
    can_router_register(can_id, rte_internal_bsw_adapter);

    /* 同时注册到 filter */
    can_router_add_filter(can_id);

    printf("[RTE] SWC callback registered for CAN ID 0x%X\n", can_id);
}

int Rte_Call_SendCanFrame(uint32_t can_id, const uint8_t *data, uint8_t len)
{
    if (rte_can_fd < 0)
        return -1;

    struct can_frame frame;
    memset(&frame, 0, sizeof(frame));

    frame.can_id = can_id;
    frame.can_dlc = (len > 8) ? 8 : len;
    memcpy(frame.data, data, frame.can_dlc);

    return can_send(rte_can_fd, &frame);
}
