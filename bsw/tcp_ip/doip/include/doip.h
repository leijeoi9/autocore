#ifndef DOIP_H
#define DOIP_H

/**
 * @file doip.h
 * @brief DoIP (Diagnostic over Internet Protocol) — ISO 13400
 *
 * DoIP 通过 TCP/IP (端口 13400) 传输 UDS 诊断报文，提供比 CAN 更高的带宽。
 * 支持车辆自动发现（通过 UDP 广播）。
 *
 * 核心功能：
 *   1. 车辆识别 — 诊断仪发现网络中的 ECU
 *   2. 路由激活 — 诊断仪注册到 ECU，获得诊断权限
 *   3. 诊断消息 — 在 TCP 连接上传输 UDS 报文
 *
 * DoIP 报文头 (8 字节):
 *   [协议版本][反向版本][负载类型 2字节][负载长度 4字节][负载...]
 */

#include <stdint.h>

/* ==================== 常量定义 ==================== */

/** DoIP 默认端口 */
#define DOIP_DEFAULT_PORT 13400

/** DoIP 协议版本 (ISO 13400-2:2012) */
#define DOIP_PROTOCOL_VERSION    0x02
#define DOIP_INVERSE_VERSION     0xFD   /* ~0x02 */

/* ==================== 负载类型 (Payload Types) ==================== */

/** 通用 DoIP 头否定确认 */
#define DOIP_GENERIC_DOIP_NACK           0x0000

/** 车辆识别请求 */
#define DOIP_VEHICLE_ID_REQUEST          0x0001

/** 车辆识别响应 (VIN + 逻辑地址) */
#define DOIP_VEHICLE_ID_RESPONSE         0x0002

/** 车辆识别请求 (通过 EID) */
#define DOIP_VEHICLE_ID_REQUEST_EID      0x0003

/** 车辆识别响应 (通过 EID) */
#define DOIP_VEHICLE_ID_RESPONSE_EID     0x0004

/** 车辆识别请求 (通过 VIN) */
#define DOIP_VEHICLE_ID_REQUEST_VIN      0x0005

/** 车辆识别响应 (通过 VIN) */
#define DOIP_VEHICLE_ID_RESPONSE_VIN     0x0006

/** 路由激活请求 */
#define DOIP_ROUTING_ACTIVATION_REQUEST  0x0007

/** 路由激活响应 */
#define DOIP_ROUTING_ACTIVATION_RESPONSE 0x0008

/** 诊断消息 (TCP) */
#define DOIP_DIAGNOSTIC_MESSAGE          0x8001

/** 诊断消息确认 */
#define DOIP_DIAGNOSTIC_MESSAGE_ACK      0x8002

/* ==================== NACK 码 ==================== */

#define DOIP_NACK_INVALID_HEADER         0x00  /* 无效头 */
#define DOIP_NACK_UNKNOWN_PAYLOAD        0x01  /* 未知负载类型 */
#define DOIP_NACK_INVALID_LENGTH         0x02  /* 无效长度 */
#define DOIP_NACK_INVALID_VERSION        0x04  /* 协议版本不匹配 */

/* ==================== 路由激活响应码 ==================== */

#define DOIP_ROUTE_ACCEPTED              0x10  /* 路由激活成功 */
#define DOIP_ROUTE_DENIED                0x11  /* 拒绝 */
#define DOIP_ROUTE_TEMP_DENIED           0x12  /* 临时拒绝 */

/* ==================== 结构定义 ==================== */

/** DoIP 报文头 (8 字节) */
typedef struct __attribute__((packed)) {
    uint8_t  protocol_version;         /* 协议版本 */
    uint8_t  inverse_version;          /* 反向版本 */
    uint16_t payload_type;             /* 负载类型 */
    uint32_t payload_length;           /* 负载长度 (网络字节序) */
} doip_header_t;

/** DoIP 车辆识别响应 */
typedef struct __attribute__((packed)) {
    uint8_t  vin[17];                  /* VIN 码 (17 字节) */
    uint8_t  logical_addr[2];          /* 逻辑地址 (2 字节) */
    uint8_t  eid[6];                   /* EID (6 字节) */
    uint8_t  gid[6];                   /* GID (6 字节) */
} doip_vehicle_id_resp_t;

/** DoIP 路由激活请求 */
typedef struct __attribute__((packed)) {
    uint8_t  source_addr[2];           /* 源地址 (2 字节) */
    uint8_t  activation_type;          /* 激活类型 (1 字节) */
    uint8_t  reserved[5];              /* 保留 (5 字节) */
} doip_routing_act_req_t;

/** DoIP 路由激活响应 */
typedef struct __attribute__((packed)) {
    uint8_t  source_addr[2];           /* 源地址 */
    uint8_t  target_addr[2];           /* 目标地址 (ECU 地址) */
    uint8_t  route_result;             /* 路由激活结果 */
    uint8_t  reserved[7];              /* 保留 */
} doip_routing_act_resp_t;

/* ==================== DoIP 句柄 ==================== */

typedef struct {
    int udp_fd;                        /* UDP socket (车辆发现) */
    int tcp_fd;                        /* TCP server socket */
    int diagnostic_fd;                 /* 当前诊断 TCP 连接 */
    int routing_active;                /* 路由是否已激活 */

    char vin[17 + 1];                  /* 本机 VIN */
    uint16_t logical_addr;             /* 本机逻辑地址 */
} doip_handle_t;

/* ==================== 接口函数 ==================== */

/**
 * @brief 初始化 DoIP 服务
 *
 * @param handle        DoIP 句柄
 * @param vin           车辆 VIN 码 (17 字符)
 * @param logical_addr  逻辑地址
 * @return 0 成功, -1 失败
 */
int doip_init(doip_handle_t *handle, const char *vin, uint16_t logical_addr);

/**
 * @brief DoIP 主循环处理
 *
 * 每帧调用，处理：
 *   1. UDP 车辆发现请求
 *   2. TCP 新连接
 *   3. TCP 诊断消息
 *
 * @param handle   DoIP 句柄
 * @param uds_callback 当收到 UDS 诊断消息时调用此回调处理
 *                     回调格式: (request, req_len, response) → 0=成功
 */
void doip_poll(doip_handle_t *handle,
                int (*uds_callback)(const uint8_t *req, uint16_t req_len,
                                    uint8_t *resp, uint16_t *resp_len));

/**
 * @brief 关闭 DoIP 服务
 */
void doip_close(doip_handle_t *handle);

#endif /* DOIP_H */
