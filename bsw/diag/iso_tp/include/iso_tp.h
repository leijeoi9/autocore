#ifndef ISO_TP_H
#define ISO_TP_H

/**
 * @file iso_tp.h
 * @brief ISO 15765-2 (ISO-TP) — CAN Transport Layer
 *
 * 在 CAN 2.0 之上提供传输层能力，解决单帧最多 8 字节的限制。
 *
 * 帧类型：
 *   - 单帧 (SF)   : 报文长度 ≤ 7 字节，一帧发完
 *   - 首帧 (FF)   : 报文长度 > 7 字节，声明总长度 + 前 6 字节数据
 *   - 流控帧 (FC) : 接收方控制发送方的节奏（BS=块大小, ST=最小间隔）
 *   - 连续帧 (CF) : 后续数据帧，从 1 开始编号，到 F 后回绕到 0
 *
 * 使用示例：
 * @code
 *   int fd = can_open("vcan0");
 *   isotp_init(fd);
 *
 *   // 发送 20 字节数据（自动拆成 FF + 多个 CF）
 *   uint8_t data[] = {1,2,3,...,20};
 *   isotp_send(0x7E0, data, 20);
 *
 *   // 主循环中轮询
 *   while (1) {
 *       isotp_poll();
 *       uint8_t rx_buf[4096];
 *       int len = isotp_receive(NULL, rx_buf, sizeof(rx_buf));
 *       if (len > 0) {
 *           // 收到一个完整的 ISO-TP 报文
 *       }
 *   }
 * @endcode
 */

#include <stdint.h>
#include <linux/can.h>

/* ==================== 常量定义 ==================== */

/** ISO-TP 帧类型（PCI 高 nibble） */
#define ISO_TP_PCI_SF     0x00  /* 单帧 (Single Frame)    */
#define ISO_TP_PCI_FF     0x10  /* 首帧 (First Frame)     */
#define ISO_TP_PCI_CF     0x20  /* 连续帧 (Consecutive)   */
#define ISO_TP_PCI_FC     0x30  /* 流控帧 (Flow Control)  */

/** 流控状态 */
#define ISO_TP_FC_CTS     0x00  /* Continue to Send (继续) */
#define ISO_TP_FC_WT      0x01  /* Wait (等待)             */
#define ISO_TP_FC_OVFLW   0x02  /* Overflow (溢出/中止)    */

/** 最大可发送/接收的 ISO-TP 报文长度（UDS 最大 4095 字节） */
#define ISO_TP_MAX_LEN    4095

/** 单帧最大数据长度 */
#define ISO_TP_SF_MAX_LEN 7

/** 首帧头占 2 字节，留给数据的空间 */
#define ISO_TP_FF_DATA_LEN 6

/** 连续帧最大数据长度 */
#define ISO_TP_CF_DATA_LEN 7

/** 默认超时时间 (ms) */
#define ISO_TP_DEFAULT_TIMEOUT 1000

/* ==================== 发送缓冲区 ==================== */

/** 发送状态机 */
typedef enum {
    ISO_TP_TX_IDLE,           /* 空闲 */
    ISO_TP_TX_WAIT_FC,        /* 已发首帧，等待流控帧 */
    ISO_TP_TX_SENDING_CF,     /* 正在发送连续帧 */
} isotp_tx_state_t;

/** 接收状态机 */
typedef enum {
    ISO_TP_RX_IDLE,           /* 空闲 */
    ISO_TP_RX_WAITING_CF,     /* 已收首帧，等待连续帧 */
} isotp_rx_state_t;

/* ==================== 接口函数 ==================== */

/**
 * @brief 初始化 ISO-TP 层
 *
 * @param can_fd 已打开的 CAN socket 文件描述符
 */
void isotp_init(int can_fd);

/**
 * @brief 发送 ISO-TP 报文（自动选择 SF 或 FF+CF 模式）
 *
 * @param can_id      目标 CAN ID
 * @param data        数据缓冲区
 * @param len         数据长度
 * @return 实际发送的字节数，负数表示错误
 *         -1: 参数错误
 *         -2: 发送状态机忙（上一次还没发完）
 */
int isotp_send(uint32_t can_id, const uint8_t *data, uint32_t len);

/**
 * @brief 接收一个完整的 ISO-TP 报文
 *
 * 必须在主循环中调用 isotp_poll() 驱动接收状态机。
 * 此函数只在有完整报文时才返回数据。
 *
 * @param[out] can_id  报文的 CAN ID
 * @param[out] buf     数据缓冲区
 * @param[in]  buf_size 缓冲区大小
 * @return 实际接收的字节数
 *         0:  还没收到完整报文
 *         -1: 缓冲区太小
 *         -2: 接收超时或错误
 */
int isotp_receive(uint32_t *can_id, uint8_t *buf, uint32_t buf_size);

/**
 * @brief ISO-TP 主循环处理
 *
 * 每帧调用，驱动发送和接收状态机。
 * 内部调用 can_recv() 检查是否有新报文。
 */
void isotp_poll(void);

/**
 * @brief 检查发送状态机是否空闲
 *
 * @return 1=空闲可发新报文, 0=正在发送中
 */
int isotp_tx_idle(void);

/**
 * @brief 从外部送入一帧 CAN 数据供 ISO-TP 处理
 *
 * 当上层（如 RTE/CanIf）已经通过 can_recv 收到了一帧 CAN 数据，
 * 并且识别出这是诊断报文时，调用此函数将帧交给 ISO-TP 状态机处理。
 *
 * 这样避免了多个模块同时 poll 同一个 CAN fd 导致数据丢失的问题。
 *
 * @param frame  收到的 CAN 帧
 */
void isotp_rx_frame(const struct can_frame *frame);

#endif /* ISO_TP_H */
