#include "can_dispatcher.h"
#include "can_filter.h"

#define MAX_CAN_HANDLER 32

typedef struct {
    int can_id;
    can_rx_callback_t cb;
} can_handler_t;

static can_handler_t handlers[MAX_CAN_HANDLER];

static int handler_count = 0;

void can_register(
    int can_id,
    can_rx_callback_t cb)
{
    if (handler_count >= MAX_CAN_HANDLER)
        return;

    handlers[handler_count].can_id = can_id;
    handlers[handler_count].cb = cb;

    handler_count++;
}

void can_dispatch(
    const struct can_frame *frame)
{
    for (int i = 0; i < handler_count; i++) {

        if (can_filter_match(frame->can_id) &&handlers[i].can_id == frame->can_id)
        {
            handlers[i].cb(frame);
        }
    }
}