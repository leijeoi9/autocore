#ifndef SOCKET_ADAPTER_H
#define SOCKET_ADAPTER_H

/**
 * @file socket_adapter.h
 * @brief Socket Adapter — Linux Socket API 封装层
 *
 * 提供 TCP Server/Client 的统一接口，屏蔽 socket/bind/listen/accept 的细节。
 * 上层模块（如 DoIP）不直接调用 Linux Socket API，而是通过此层。
 *
 * AUTOSAR 中类似的模块是 SocketAdp (Socket Adapter)，属于 BSW 的服务层。
 *
 * 使用示例：
 * @code
 *   // 启动 TCP Server
 *   socket_server_t svr;
 *   socket_server_init(&svr, 13400);
 *
 *   // 主循环中接受连接
 *   socket_client_t client;
 *   if (socket_server_accept(&svr, &client, 0) > 0) {
 *       // 处理客户端
 *       socket_client_recv(&client, buf, sizeof(buf));
 *   }
 * @endcode
 */

#include <stdint.h>

#include <netinet/in.h>

/** 最大客户端数量 */
#define SOCKET_MAX_CLIENTS 4

/** 缓冲区大小 */
#define SOCKET_BUF_SIZE 4096

/** TCP Server 句柄 */
typedef struct {
    int server_fd;                          /* listen socket fd */
    uint16_t port;                          /* 监听端口 */
} socket_server_t;

/** TCP 客户端连接句柄 */
typedef struct {
    int fd;                                 /* 连接 socket fd */
    struct sockaddr_in addr;                /* 客户端地址 */
    int active;                             /* 1=活跃, 0=已断开 */
} socket_client_t;

/**
 * @brief 初始化 TCP Server
 *
 * @param server   Server 句柄
 * @param port     监听端口
 * @return 0 成功, -1 失败
 */
int socket_server_init(socket_server_t *server, uint16_t port);

/**
 * @brief 接受客户端连接（非阻塞）
 *
 * @param server   Server 句柄
 * @param client   客户端句柄（输出）
 * @param timeout_ms 超时毫秒 (0=非阻塞, -1=阻塞)
 * @return 1 有新的客户端连接, 0 无连接, -1 错误
 */
int socket_server_accept(socket_server_t *server,
                          socket_client_t *client,
                          int timeout_ms);

/**
 * @brief 关闭 Server
 */
void socket_server_close(socket_server_t *server);

/**
 * @brief 从客户端接收数据（非阻塞）
 *
 * @param client 客户端句柄
 * @param buf    接收缓冲区
 * @param size   缓冲区大小
 * @return 接收的字节数, 0=无数据, -1=断开/错误
 */
int socket_client_recv(socket_client_t *client, uint8_t *buf, int size);

/**
 * @brief 向客户端发送数据
 *
 * @param client 客户端句柄
 * @param data   数据
 * @param len    数据长度
 * @return 发送的字节数, -1=错误
 */
int socket_client_send(socket_client_t *client,
                        const uint8_t *data, int len);

/**
 * @brief 断开客户端连接
 */
void socket_client_close(socket_client_t *client);

/**
 * @brief 创建一个 TCP Socket 并连接到指定地址
 *
 * @param sockfd   输出 socket fd
 * @param ip       IP 地址字符串 (如 "192.168.1.100")
 * @param port     端口
 * @return 0 成功, -1 失败
 */
int socket_client_connect(int *sockfd, const char *ip, uint16_t port);

#endif /* SOCKET_ADAPTER_H */
