#ifndef CAN_DRIVER_H
#define CAN_DRIVER_H


#include <linux/can.h>

int can_open(const char *ifname);
int can_send(int fd, const struct can_frame *frame);
int can_recv(int fd, struct can_frame *frame);
void can_close(int fd);

#endif