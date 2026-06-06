#ifndef CAN_SIGNAL_H
#define CAN_SIGNAL_H

#include <linux/can.h>

typedef struct {
    int can_id;

    int start_bit;
    int length;       // bits

    float factor;
    float offset;

    const char *name;
} can_signal_desc_t;

typedef struct {
    float engine_speed;
    float vehicle_speed;
    float engine_temp;
} can_signal_t;

void can_decode(const struct can_frame *frame, can_signal_t *sig);

#endif