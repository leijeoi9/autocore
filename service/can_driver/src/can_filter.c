#include "can_filter.h"

#define MAX_CAN_FILTER 32

static int filter_ids[MAX_CAN_FILTER];

static int filter_count = 0;

int can_filter_add(int can_id)
{
    if (filter_count >= MAX_CAN_FILTER)
        return -1;

    for (int i = 0; i < filter_count; i++) {
        if (filter_ids[i] == can_id)
            return 0;  // already added
    }

    filter_ids[filter_count++] = can_id;

    return 0;
}

int can_filter_match(int can_id)
{
    for (int i = 0; i < filter_count; i++) {

        if (filter_ids[i] == can_id)
            return 1;
    }

    return 0;
}