#ifndef CAN_MESSAGE_H
#define CAN_MESSAGE_H

#include <linux/can.h>

void can_message_process(
    const struct can_frame *frame);

#endif