#ifndef CAN_ROUTER_H
#define CAN_ROUTER_H

#include <linux/can.h>
#include "can_signal.h"
#include "can_dispatcher.h"

void can_router_init(int fd);

void can_router_poll(void);

void can_router_register(int can_id, can_rx_callback_t cb);

void can_router_add_filter(int can_id);

void can_router_add_tx(int period_ms, const struct can_frame *frame);

#endif