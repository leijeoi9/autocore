#ifndef CAN_MESSAGE_DB_H
#define CAN_MESSAGE_DB_H

#include <linux/can.h>

typedef void (*can_message_handler_t)(
    const struct can_frame *frame);

typedef struct {

    int can_id;

    const char *name;

    can_message_handler_t handler;

} can_message_desc_t;

extern can_message_desc_t can_message_db[];

extern const int can_message_db_size;

const can_message_desc_t *
find_message_desc(int can_id);

#endif