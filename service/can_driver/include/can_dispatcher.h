#ifndef CAN_DISPATCHER_H
#define CAN_DISPATCHER_H

#include <linux/can.h>

typedef void (*can_rx_callback_t)(
    const struct can_frame *frame);

void can_register(
    int can_id,
    can_rx_callback_t cb);

void can_dispatch(
    const struct can_frame *frame);

#endif