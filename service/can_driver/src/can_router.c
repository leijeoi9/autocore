#include "can_router.h"
#include "can_driver.h"
#include "can_dispatcher.h"
#include "can_scheduler.h"
#include "can_filter.h"
#include <string.h>
#include "can_message.h"

static int g_fd;

static struct can_frame rx_frame;

void can_router_init(int fd)
{
    g_fd = fd;
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

if (can_filter_match(rx_frame.can_id)) {

    can_dispatch(&rx_frame);

    can_message_process(&rx_frame);
}
}
}