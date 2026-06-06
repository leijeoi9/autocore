#include "can_message_handler.h"
#include "can_signal.h"
#include "rte_vehicle_data.h"

#include <stdio.h>

static can_signal_t sig;

void test_message_handler(
    const struct can_frame *frame)
{
    can_decode(frame, &sig);

       g_vehicle_data.engine_speed = sig.engine_speed;

       g_vehicle_data.vehicle_speed = sig.vehicle_speed;

       g_vehicle_data.engine_temp = sig.engine_temp;

    printf("[SIGNAL] engine_speed=%.2f rpm\n",
           sig.engine_speed);

    printf("[SIGNAL] vehicle_speed=%.2f km/h\n",
           sig.vehicle_speed);

    printf("[SIGNAL] engine_temp=%.2f C\n",
           sig.engine_temp);
}