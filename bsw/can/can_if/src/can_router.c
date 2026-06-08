#include "can_router.h"
#include "can_driver.h"
#include "can_dispatcher.h"
#include "can_scheduler.h"
#include "can_filter.h"
#include <string.h>
#include "can_message.h"
#include "iso_tp.h"        /* 诊断报文路由 */

/* 诊断请求/响应的 CAN ID（与 uds_init 保持一致） */
#define CAN_DIAG_REQ_ID  0x7E0
#define CAN_DIAG_RESP_ID 0x7E8

static int g_fd;

static struct can_frame rx_frame;

void can_router_init(int fd)
{
    g_fd = fd;
    isotp_init(fd);
    printf("[CanIf] ISO-TP initialized on fd=%d\n", fd);
}

void can_router_register(int can_id, can_rx_callback_t cb)
{
    can_register(can_id, cb);
}

void can_router_add_filter(int can_id)
{
    can_filter_add(can_id);
}

void can_router_add_tx(int period_ms, const struct can_frame *frame)
{
    can_scheduler_add(period_ms, frame);
}

void can_router_poll(void)
{
    can_scheduler_run(g_fd);

    if (can_recv(g_fd, &rx_frame) > 0) {

        /* 调试：打印收到的所有帧 */
        printf("[RTR] RX: id=0x%X, dlc=%d, data[0]=0x%02X\n",
               rx_frame.can_id, rx_frame.can_dlc, rx_frame.data[0]);

        /* ---- 诊断报文路由：直接交给 ISO-TP ---- */
        if (rx_frame.can_id == CAN_DIAG_REQ_ID ||
            rx_frame.can_id == CAN_DIAG_RESP_ID) {
            printf("[RTR] Routing to ISO-TP\n");
            isotp_rx_frame(&rx_frame);
            return;     /* 不经过普通过滤/分发/解码路径 */
        }

        if (can_filter_match(rx_frame.can_id)) {
            can_dispatch(&rx_frame);
            can_message_process(&rx_frame);
        }
    }
}