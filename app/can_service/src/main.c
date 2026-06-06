#include <stdio.h>
#include <unistd.h>

#include "can_driver.h"
#include "can_signal.h"
#include "logger.h"
#include "can_router.h"

static void test_handler(const struct can_frame *frame)
{
    printf("[DISPATCH] CAN ID = 0x%X\n", frame->can_id);
}

static void engine_handler(const struct can_frame *frame)
{
    printf("[ENGINE] frame received: 0x%X\n",
           frame->can_id);
}

static void vehicle_handler(const struct can_frame *frame)
{
    printf("[VEHICLE] frame received: 0x%X\n",
           frame->can_id);
}

int main(void)
{
    int fd = can_open("vcan0");

    if (fd < 0) {
        return -1;
    }

    can_router_init(fd);

    struct can_frame tx_frame = {0};

    tx_frame.can_id = 0x555;
    tx_frame.can_dlc = 2;
    tx_frame.data[0] = 0xAA;
    tx_frame.data[1] = 0x55;

    can_router_add_tx(1000, &tx_frame);

    struct can_frame heartbeat = {0};

    heartbeat.can_id = 0x700;
    heartbeat.can_dlc = 1;
    heartbeat.data[0] = 0x01;

    can_router_add_tx(500, &heartbeat);

    can_router_add_filter(0x123);
    can_router_add_filter(0x555);

    can_router_register(0x100, engine_handler);
    can_router_register(0x200, vehicle_handler);
    can_router_register(0x123, test_handler);

    while (1) {
        can_router_poll();
        usleep(1000);
    }

    return 0;
}