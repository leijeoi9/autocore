#include "can_message.h"
#include "can_message_db.h"

#include <stdio.h>

void can_message_process(
    const struct can_frame *frame)
{
    const can_message_desc_t *desc;

    desc = find_message_desc(frame->can_id);

    if (!desc)
        return;

    printf("[MSG] %s (0x%X)\n",
           desc->name,
           frame->can_id);

    if (desc->handler)
        desc->handler(frame);
}