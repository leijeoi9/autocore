#include "doip.h"
#include "socket_adapter.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

/* ==================== 辅助函数 ==================== */

/** 写入 2 字节到缓冲区 (网络字节序) */
static inline void write16(uint8_t *buf, uint16_t val)
{
    buf[0] = (val >> 8) & 0xFF;
    buf[1] = val & 0xFF;
}

/** 写入 4 字节到缓冲区 (网络字节序) */
static inline void write32(uint8_t *buf, uint32_t val)
{
    buf[0] = (val >> 24) & 0xFF;
    buf[1] = (val >> 16) & 0xFF;
    buf[2] = (val >> 8) & 0xFF;
    buf[3] = val & 0xFF;
}

/** 读取 2 字节 (网络字节序) */
static inline uint16_t read16(const uint8_t *buf)
{
    return ((uint16_t)buf[0] << 8) | buf[1];
}

/** 读取 4 字节 (网络字节序) */
static inline uint32_t read32(const uint8_t *buf)
{
    return ((uint32_t)buf[0] << 24) |
           ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] << 8)  |
           buf[3];
}

/** 构建 DoIP 报文头到缓冲区 */
static void build_header(uint8_t *buf, uint16_t payload_type,
                          uint32_t payload_length)
{
    buf[0] = DOIP_PROTOCOL_VERSION;
    buf[1] = DOIP_INVERSE_VERSION;
    write16(&buf[2], payload_type);
    write32(&buf[4], payload_length);
}

/** 发送 DoIP 报文到 TCP 连接 */
static int send_doip_message(int fd, uint16_t payload_type,
                              const uint8_t *payload, uint32_t payload_len)
{
    uint8_t header[8];
    build_header(header, payload_type, payload_len);

    /* 发送头 */
    if (write(fd, header, 8) != 8)
        return -1;

    /* 发送负载 */
    if (payload && payload_len > 0) {
        if ((int)write(fd, payload, payload_len) != (int)payload_len)
            return -1;
    }

    return 0;
}

/** 发送 UDP 车辆识别响应 */
static int send_udp_vehicle_id(int udp_fd, struct sockaddr_in *client_addr,
                                const char *vin, uint16_t logical_addr)
{
    uint8_t resp_buf[64];
    uint8_t payload[32];

    /* 构建车辆识别响应负载 */
    memset(payload, 0, sizeof(payload));
    memcpy(payload, vin, 17);                         /* VIN */
    write16(&payload[17], logical_addr);              /* 逻辑地址 */
    /* EID (6 字节) 和 GID (6 字节) 留空，实际项目使用 MAC 地址 */

    /* 构建完整报文 */
    build_header(resp_buf, DOIP_VEHICLE_ID_RESPONSE, 29);
    memcpy(&resp_buf[8], payload, 29);

    sendto(udp_fd, resp_buf, 8 + 29, 0,
           (struct sockaddr *)client_addr, sizeof(*client_addr));

    printf("[DoIP] UDP: sent vehicle ID response to %s:%d\n",
           inet_ntoa(client_addr->sin_addr),
           ntohs(client_addr->sin_port));
    return 0;
}

/* ==================== 接口实现 ==================== */

int doip_init(doip_handle_t *handle, const char *vin, uint16_t logical_addr)
{
    if (!handle || !vin)
        return -1;

    memset(handle, 0, sizeof(*handle));

    /* 保存 VIN 和逻辑地址 */
    strncpy(handle->vin, vin, 17);
    handle->vin[17] = '\0';
    handle->logical_addr = logical_addr;

    /* ---- 创建 UDP socket（车辆发现） ---- */
    handle->udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (handle->udp_fd < 0) {
        perror("[DoIP] UDP socket");
        return -1;
    }

    struct sockaddr_in udp_addr;
    memset(&udp_addr, 0, sizeof(udp_addr));
    udp_addr.sin_family = AF_INET;
    udp_addr.sin_addr.s_addr = INADDR_ANY;
    udp_addr.sin_port = htons(DOIP_DEFAULT_PORT);

    if (bind(handle->udp_fd, (struct sockaddr *)&udp_addr,
             sizeof(udp_addr)) < 0) {
        perror("[DoIP] UDP bind");
        close(handle->udp_fd);
        handle->udp_fd = -1;
        return -1;
    }

    /* ---- 创建 TCP socket（诊断消息） ---- */
    handle->tcp_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (handle->tcp_fd < 0) {
        perror("[DoIP] TCP socket");
        close(handle->udp_fd);
        return -1;
    }

    int opt = 1;
    setsockopt(handle->tcp_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in tcp_addr;
    memset(&tcp_addr, 0, sizeof(tcp_addr));
    tcp_addr.sin_family = AF_INET;
    tcp_addr.sin_addr.s_addr = INADDR_ANY;
    tcp_addr.sin_port = htons(DOIP_DEFAULT_PORT);

    if (bind(handle->tcp_fd, (struct sockaddr *)&tcp_addr,
             sizeof(tcp_addr)) < 0) {
        perror("[DoIP] TCP bind");
        close(handle->udp_fd);
        close(handle->tcp_fd);
        return -1;
    }

    if (listen(handle->tcp_fd, 1) < 0) {
        perror("[DoIP] TCP listen");
        close(handle->udp_fd);
        close(handle->tcp_fd);
        return -1;
    }

    handle->diagnostic_fd = -1;
    handle->routing_active = 0;

    printf("[DoIP] Initialized: VIN=%s, Port=%d\n", vin, DOIP_DEFAULT_PORT);
    return 0;
}

void doip_poll(doip_handle_t *handle,
                int (*uds_callback)(const uint8_t *, uint16_t,
                                    uint8_t *, uint16_t *))
{
    if (!handle)
        return;

    fd_set read_fds;
    FD_ZERO(&read_fds);

    int max_fd = 0;

    if (handle->udp_fd >= 0) {
        FD_SET(handle->udp_fd, &read_fds);
        if (handle->udp_fd > max_fd) max_fd = handle->udp_fd;
    }
    if (handle->tcp_fd >= 0) {
        FD_SET(handle->tcp_fd, &read_fds);
        if (handle->tcp_fd > max_fd) max_fd = handle->tcp_fd;
    }
    if (handle->diagnostic_fd >= 0) {
        FD_SET(handle->diagnostic_fd, &read_fds);
        if (handle->diagnostic_fd > max_fd) max_fd = handle->diagnostic_fd;
    }

    struct timeval tv = {0, 0};  /* 非阻塞 */
    int ret = select(max_fd + 1, &read_fds, NULL, NULL, &tv);

    if (ret <= 0)
        return;

    /* ---- 1. UDP 车辆发现请求 ---- */
    if (handle->udp_fd >= 0 && FD_ISSET(handle->udp_fd, &read_fds)) {
        uint8_t udp_buf[64];
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        int n = (int)recvfrom(handle->udp_fd, udp_buf, sizeof(udp_buf), 0,
                               (struct sockaddr *)&client_addr, &addr_len);
        if (n > 0) {
            uint16_t payload_type = read16(&udp_buf[2]);

            /* 处理车辆识别请求 */
            if (payload_type == DOIP_VEHICLE_ID_REQUEST) {
                send_udp_vehicle_id(handle->udp_fd, &client_addr,
                                     handle->vin, handle->logical_addr);
            }
        }
    }

    /* ---- 2. TCP 新连接 ---- */
    if (handle->tcp_fd >= 0 && FD_ISSET(handle->tcp_fd, &read_fds)) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        int fd = accept(handle->tcp_fd,
                         (struct sockaddr *)&client_addr, &addr_len);
        if (fd >= 0) {
            printf("[DoIP] TCP client connected: %s:%d\n",
                   inet_ntoa(client_addr.sin_addr),
                   ntohs(client_addr.sin_port));

            /* 关闭旧的诊断连接 */
            if (handle->diagnostic_fd >= 0) {
                close(handle->diagnostic_fd);
            }

            handle->diagnostic_fd = fd;
            handle->routing_active = 0;  /* 需要重新激活路由 */
        }
    }

    /* ---- 3. TCP 诊断消息 ---- */
    if (handle->diagnostic_fd >= 0 &&
        FD_ISSET(handle->diagnostic_fd, &read_fds)) {

        uint8_t tcp_buf[4096];
        int n = (int)read(handle->diagnostic_fd, tcp_buf, sizeof(tcp_buf));

        if (n <= 0) {
            /* 客户端断开 */
            printf("[DoIP] TCP client disconnected\n");
            close(handle->diagnostic_fd);
            handle->diagnostic_fd = -1;
            handle->routing_active = 0;
            return;
        }

        /* 解析 DoIP 头 (至少 8 字节) */
        if (n < 8) {
            printf("[DoIP] Header too short\n");
            return;
        }

        uint8_t proto_ver  = tcp_buf[0];
        uint8_t inv_ver    = tcp_buf[1];
        uint16_t payload_type = read16(&tcp_buf[2]);
        uint32_t payload_len  = read32(&tcp_buf[4]);

        /* 验证协议版本 */
        if (proto_ver != DOIP_PROTOCOL_VERSION ||
            inv_ver != DOIP_INVERSE_VERSION) {
            printf("[DoIP] Protocol version mismatch\n");
            uint8_t nack[1] = {DOIP_NACK_INVALID_VERSION};
            send_doip_message(handle->diagnostic_fd,
                               DOIP_GENERIC_DOIP_NACK, nack, 1);
            return;
        }

        /* 检查完整报文是否到达 */
        if ((uint32_t)n < 8 + payload_len) {
            printf("[DoIP] Incomplete message: need %u, got %d\n",
                   8 + payload_len, n);
            return;
        }

        const uint8_t *payload = &tcp_buf[8];

        switch (payload_type) {

        /* ---- 路由激活请求 ---- */
        case DOIP_ROUTING_ACTIVATION_REQUEST: {
            if (payload_len < 8) {
                uint8_t nack[1] = {DOIP_NACK_INVALID_LENGTH};
                send_doip_message(handle->diagnostic_fd,
                                   DOIP_GENERIC_DOIP_NACK, nack, 1);
                break;
            }

            uint16_t source_addr = read16(&payload[0]);
            uint8_t act_type = payload[2];

            printf("[DoIP] Routing activation: src=0x%04X, type=%d\n",
                   source_addr, act_type);

            /* 构建路由激活响应 */
            uint8_t act_resp[12];
            memset(act_resp, 0, sizeof(act_resp));
            write16(&act_resp[0], source_addr);     /* 源地址 */
            write16(&act_resp[2], handle->logical_addr); /* ECU 地址 */
            act_resp[4] = DOIP_ROUTE_ACCEPTED;       /* 路由激活成功 */

            send_doip_message(handle->diagnostic_fd,
                               DOIP_ROUTING_ACTIVATION_RESPONSE,
                               act_resp, 12);

            handle->routing_active = 1;
            printf("[DoIP] Routing activated\n");
            break;
        }

        /* ---- UDS 诊断消息 ---- */
        case DOIP_DIAGNOSTIC_MESSAGE: {
            if (!handle->routing_active) {
                printf("[DoIP] Diagnostic message dropped: routing not active\n");
                break;
            }

            if (payload_len < 4) {
                break;
            }

            uint16_t src_addr = read16(&payload[0]);
            uint16_t tgt_addr = read16(&payload[2]);
            uint16_t uds_len = (uint16_t)(payload_len - 4);
            const uint8_t *uds_data = &payload[4];

            printf("[DoIP] Diagnostic msg: src=0x%04X, tgt=0x%04X, UDS len=%u\n",
                   src_addr, tgt_addr, uds_len);

            /* 调用 UDS 回调处理 */
            if (uds_callback) {
                uint8_t uds_resp[4095];
                uint16_t uds_resp_len = 0;

                if (uds_callback(uds_data, uds_len,
                                  uds_resp, &uds_resp_len) == 0) {
                    /* 构建诊断消息确认 */
                    uint8_t diag_ack[4096];
                    uint16_t ack_len = 0;

                    write16(&diag_ack[0], tgt_addr);  /* 源地址 = ECU */
                    write16(&diag_ack[2], src_addr);  /* 目标地址 = Tester */
                    diag_ack[4] = 0;                   /* 确认码: 成功 */
                    diag_ack[5] = 0;                   /* 预留 */

                    /* 前置 UDS 响应 */
                    if (uds_resp_len > 0) {
                        memcpy(&diag_ack[6], uds_resp, uds_resp_len);
                        ack_len = 6 + uds_resp_len;
                    } else {
                        ack_len = 6;
                    }

                    send_doip_message(handle->diagnostic_fd,
                                       DOIP_DIAGNOSTIC_MESSAGE_ACK,
                                       diag_ack, ack_len);
                }
            }
            break;
        }

        default:
            printf("[DoIP] Unknown payload type: 0x%04X\n", payload_type);
            uint8_t nack[1] = {DOIP_NACK_UNKNOWN_PAYLOAD};
            send_doip_message(handle->diagnostic_fd,
                               DOIP_GENERIC_DOIP_NACK, nack, 1);
            break;
        }
    }
}

void doip_close(doip_handle_t *handle)
{
    if (!handle)
        return;

    if (handle->diagnostic_fd >= 0) {
        close(handle->diagnostic_fd);
        handle->diagnostic_fd = -1;
    }
    if (handle->tcp_fd >= 0) {
        close(handle->tcp_fd);
        handle->tcp_fd = -1;
    }
    if (handle->udp_fd >= 0) {
        close(handle->udp_fd);
        handle->udp_fd = -1;
    }

    handle->routing_active = 0;
    printf("[DoIP] Closed\n");
}
