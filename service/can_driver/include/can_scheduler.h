#ifndef CAN_SCHEDULER_H
#define CAN_SCHEDULER_H

#include <linux/can.h>

typedef struct {
    int period_ms;
    long long last_run_ms;
    struct can_frame frame;
} can_schedule_item_t;

void can_scheduler_add(
    int period_ms,
    const struct can_frame *frame);

void can_scheduler_run(int fd);

#endif