#include "can_scheduler.h"
#include <time.h>
#include <stdio.h>
#include "can_driver.h"
#define MAX_SCHEDULE_ITEMS 16

static can_schedule_item_t items[MAX_SCHEDULE_ITEMS];
static int item_count = 0;

// void can_scheduler_add(
//     int period_ms,
//     const struct can_frame *frame)
// {
//     if (item_count >= MAX_SCHEDULE_ITEMS)
//         return;

//     items[item_count].period_ms = period_ms;

//     items[item_count].last_run_ms = 0;

//     items[item_count].frame = *frame;

//     item_count++;
// }
void can_scheduler_add(
    int period_ms,
    const struct can_frame *frame)
{
    if (item_count >= MAX_SCHEDULE_ITEMS)
        return;

    printf("[ADD] id=0x%X period=%d\n",
           frame->can_id,
           period_ms);

    items[item_count].period_ms = period_ms;
    items[item_count].last_run_ms = 0;
    items[item_count].frame = *frame;

    item_count++;
}
static long long get_time_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    return (long long)ts.tv_sec * 1000 +
           ts.tv_nsec / 1000000;
}

void can_scheduler_run(int fd)
{
    long long now = get_time_ms();

    for (int i = 0; i < item_count; i++) {
    if ((now - items[i].last_run_ms)>= items[i].period_ms) {

    printf("[SCHED] send 0x%X\n",
           items[i].frame.can_id);

    can_send(fd, &items[i].frame);

    items[i].last_run_ms = now;
}
    
    }
}

