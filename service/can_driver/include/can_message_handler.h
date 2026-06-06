#ifndef CAN_MESSAGE_HANDLER_H
#define CAN_MESSAGE_HANDLER_H

#include <linux/can.h>

void test_message_handler(
    const struct can_frame *frame);

#endif