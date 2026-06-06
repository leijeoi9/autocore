#include "socket_adapter.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* ==================== Server 实现 ==================== */

int socket_server_init(socket_server_t *server, uint16_t port)
{
    if (!server)
        return -1;

    memset(server, 0, sizeof(*server));

    /* 创建 TCP socket */
    server->server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server->server_fd < 0) {
        perror("socket_server_init: socket");
        return -1;
    }

    /* SO_REUSEADDR — 允许端口重用（快速重启） */
    int opt = 1;
    setsockopt(server->server_fd, SOL_SOCKET, SO_REUSEADDR,
               &opt, sizeof(opt));

    /* 绑定地址 */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;   /* 监听所有网络接口 */
    addr.sin_port = htons(port);

    if (bind(server->server_fd, (struct sockaddr *)&addr,
             sizeof(addr)) < 0) {
        perror("socket_server_init: bind");
        close(server->server_fd);
        server->server_fd = -1;
        return -1;
    }

    /* 开始监听 */
    if (listen(server->server_fd, SOCKET_MAX_CLIENTS) < 0) {
        perror("socket_server_init: listen");
        close(server->server_fd);
        server->server_fd = -1;
        return -1;
    }

    server->port = port;

    printf("[SOCKET] TCP Server listening on port %u\n", port);
    return 0;
}

int socket_server_accept(socket_server_t *server,
                          socket_client_t *client,
                          int timeout_ms)
{
    if (!server || !client || server->server_fd < 0)
        return -1;

    /* 使用 select 实现超时 */
    if (timeout_ms > 0) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(server->server_fd, &read_fds);

        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;

        int ret = select(server->server_fd + 1, &read_fds,
                          NULL, NULL, &tv);
        if (ret <= 0) {
            return 0;  /* 超时或无连接 */
        }
    }

    /* 接受连接 */
    socklen_t addr_len = sizeof(client->addr);
    memset(&client->addr, 0, sizeof(client->addr));

    int fd = accept(server->server_fd,
                    (struct sockaddr *)&client->addr,
                    &addr_len);
    if (fd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return 0;
        return -1;
    }

    client->fd = fd;
    client->active = 1;

    printf("[SOCKET] Client connected: %s:%d\n",
           inet_ntoa(client->addr.sin_addr),
           ntohs(client->addr.sin_port));
    return 1;
}

void socket_server_close(socket_server_t *server)
{
    if (server && server->server_fd >= 0) {
        close(server->server_fd);
        server->server_fd = -1;
        printf("[SOCKET] Server closed\n");
    }
}

/* ==================== Client 实现 ==================== */

int socket_client_recv(socket_client_t *client, uint8_t *buf, int size)
{
    if (!client || !client->active || client->fd < 0 || !buf || size <= 0)
        return -1;

    int n = (int)read(client->fd, buf, (size_t)size);

    if (n > 0) {
        return n;
    } else if (n == 0) {
        /* 对方关闭连接 */
        printf("[SOCKET] Client disconnected\n");
        client->active = 0;
        close(client->fd);
        client->fd = -1;
        return -1;
    } else {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return 0;
        client->active = 0;
        close(client->fd);
        client->fd = -1;
        return -1;
    }
}

int socket_client_send(socket_client_t *client,
                        const uint8_t *data, int len)
{
    if (!client || !client->active || client->fd < 0 || !data || len <= 0)
        return -1;

    int n = (int)write(client->fd, data, (size_t)len);

    if (n < 0) {
        perror("socket_client_send");
        client->active = 0;
        close(client->fd);
        client->fd = -1;
        return -1;
    }

    return n;
}

void socket_client_close(socket_client_t *client)
{
    if (client && client->fd >= 0) {
        close(client->fd);
        client->fd = -1;
        client->active = 0;
        printf("[SOCKET] Client closed\n");
    }
}

int socket_client_connect(int *sockfd, const char *ip, uint16_t port)
{
    if (!sockfd || !ip)
        return -1;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket_client_connect: socket");
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &addr.sin_addr);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("socket_client_connect: connect");
        close(fd);
        return -1;
    }

    *sockfd = fd;
    printf("[SOCKET] Connected to %s:%u\n", ip, port);
    return 0;
}
