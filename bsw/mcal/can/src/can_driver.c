#include "can_driver.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <sys/socket.h>
#include <sys/ioctl.h>   // ✔ 必须
#include <net/if.h>       // ✔ struct ifreq + SIOCGIFINDEX

#include <linux/can.h>
#include <linux/can/raw.h>

int can_send(int fd, const struct can_frame *frame)
{
    int nbytes = write(fd, frame, sizeof(struct can_frame));
    if (nbytes != sizeof(struct can_frame)) {
        perror("can_send");
        return -1;
    }
    return 0;
}

int can_recv(int fd, struct can_frame *frame)
{
    int nbytes = read(fd, frame, sizeof(struct can_frame));

    if (nbytes < 0) {

        if (errno == EAGAIN ||
            errno == EWOULDBLOCK) {

            return 0;
        }

        perror("can_recv");
        return -1;
    }

    return nbytes;
}

void can_close(int fd)
{
    close(fd);
}
int can_open(const char *ifname)
{
    int fd;
    struct ifreq ifr;
    struct sockaddr_can addr;

    fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    fcntl(fd, F_SETFL, O_NONBLOCK);

    strcpy(ifr.ifr_name, ifname);
    if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
        perror("ioctl");
        close(fd);
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(fd);
        return -1;
    }

    return fd;
}
