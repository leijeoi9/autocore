#include "iso_tp.h"

#include "can_driver.h"   /* MCAL: can_recv, can_send */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <linux/can.h>

/* ==================== 内部状态 ==================== */

/** CAN 文件描述符 */
static int g_fd = -1;

/* ---------- 发送状态 ---------- */
static isotp_tx_state_t tx_state = ISO_TP_TX_IDLE;

/** 发送目标 CAN ID */
static uint32_t tx_id;

/** 待发送数据的完整缓冲 */
static uint8_t tx_buffer[ISO_TP_MAX_LEN];
static uint32_t tx_total_len;

/** 已经发送了多少字节 */
static uint32_t tx_sent;

/** 下一个连续帧的序号 (1~15, 回绕到0) */
static uint8_t tx_seq;

/** 等待 FC 时的超时时间戳 */
static long long tx_timeout_ms;

/* ---------- 接收状态 ---------- */
static isotp_rx_state_t rx_state = ISO_TP_RX_IDLE;

/** 接收到的 CAN ID */
static uint32_t rx_id;

/** 正在接收的完整数据缓冲 */
static uint8_t rx_buffer[ISO_TP_MAX_LEN];
static uint32_t rx_total_len;   /* 总长度 */
static uint32_t rx_received;    /* 已接收长度 */

/** 期待的下一帧序号 */
static uint8_t rx_exp_seq;

/** FC 控制参数 */
static uint8_t rx_bs;           /* 块大小 */
static uint8_t rx_st;           /* 最小间隔 (ms) */
static int rx_bs_count;         /* 当前块内已收帧数 */

/** 接收超时时间戳 */
static long long rx_timeout_ms;

/* ==================== 工具函数 ==================== */

static long long get_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* ==================== 帧构建函数 ==================== */

/**
 * 构建单帧 (SF)
 * PCI 格式: [ 0 | len(4bit) ] [ data... ]
 */
static void build_sf(struct can_frame *frame, uint32_t can_id,
                      const uint8_t *data, uint32_t len)
{
    memset(frame, 0, sizeof(*frame));
    frame->can_id = can_id;
    frame->can_dlc = len + 1;  /* PCI 占 1 字节 */

    frame->data[0] = ISO_TP_PCI_SF | (len & 0x0F);
    memcpy(&frame->data[1], data, len);
}

/**
 * 构建首帧 (FF)
 * PCI 格式: [ 10 | len(14bit high) ] [ len(8bit low) ] [ data... ]
 */
static void build_ff(struct can_frame *frame, uint32_t can_id,
                      const uint8_t *data, uint32_t len)
{
    memset(frame, 0, sizeof(*frame));
    frame->can_id = can_id;
    frame->can_dlc = ISO_TP_FF_DATA_LEN + 2;  /* PCI 2 字节 + 数据 */

    frame->data[0] = ISO_TP_PCI_FF | ((len >> 8) & 0x0F);
    frame->data[1] = len & 0xFF;
    memcpy(&frame->data[2], data, ISO_TP_FF_DATA_LEN);
}

/**
 * 构建流控帧 (FC)
 * PCI 格式: [ 11 | FC_Status ] [ BS ] [ ST ]
 */
static void build_fc(struct can_frame *frame, uint32_t can_id,
                      uint8_t fc_status, uint8_t bs, uint8_t st)
{
    memset(frame, 0, sizeof(*frame));
    frame->can_id = can_id;
    frame->can_dlc = 3;  /* FC 固定 3 字节 */

    frame->data[0] = ISO_TP_PCI_FC | (fc_status & 0x0F);
    frame->data[1] = bs;
    frame->data[2] = st;
}

/**
 * 构建连续帧 (CF)
 * PCI 格式: [ 01 | seq(4bit) ] [ data... ]
 */
static void build_cf(struct can_frame *frame, uint32_t can_id,
                      uint8_t seq, const uint8_t *data, uint32_t len)
{
    memset(frame, 0, sizeof(*frame));
    frame->can_id = can_id;
    frame->can_dlc = len + 1;  /* PCI 1 字节 + 数据 */

    frame->data[0] = ISO_TP_PCI_CF | (seq & 0x0F);
    memcpy(&frame->data[1], data, len);
}

/* ==================== 解析函数 ==================== */

/**
 * 解析收到的帧，获取 PCI 类型
 */
static inline uint8_t get_pci_type(uint8_t first_byte)
{
    return first_byte & 0xF0;
}

/**
 * 解析单帧或首帧的长度
 */
static uint32_t get_frame_length(const struct can_frame *frame)
{
    uint8_t pci_type = get_pci_type(frame->data[0]);

    if (pci_type == ISO_TP_PCI_SF) {
        return frame->data[0] & 0x0F;
    }

    if (pci_type == ISO_TP_PCI_FF) {
        return ((uint32_t)(frame->data[0] & 0x0F) << 8) | frame->data[1];
    }

    return 0;
}

/* ==================== 接口实现 ==================== */

void isotp_init(int can_fd)
{
    g_fd = can_fd;
    tx_state = ISO_TP_TX_IDLE;
    rx_state = ISO_TP_RX_IDLE;

    printf("[ISO_TP] initialized (fd=%d)\n", g_fd);
}

int isotp_send(uint32_t can_id, const uint8_t *data, uint32_t len)
{
    struct can_frame frame;

    if (!data || len == 0 || len > ISO_TP_MAX_LEN)
        return -1;

    if (tx_state != ISO_TP_TX_IDLE) {
        printf("[ISO_TP] TX busy, cannot send\n");
        return -2;
    }

    /* 保存到发送缓冲区 */
    tx_id = can_id;
    tx_total_len = len;
    memcpy(tx_buffer, data, len);

    if (len <= ISO_TP_SF_MAX_LEN) {
        /* —— 单帧 (SF) —— */
        build_sf(&frame, can_id, data, len);
        can_send(g_fd, &frame);
        tx_state = ISO_TP_TX_IDLE;  /* 发完立即空闲 */

        printf("[ISO_TP] TX SF: id=0x%X, len=%lu\n", can_id, (unsigned long)len);
        return (int)len;

    } else {
        /* —— 首帧 (FF) —— */
        build_ff(&frame, can_id, data, len);
        can_send(g_fd, &frame);

        tx_sent = ISO_TP_FF_DATA_LEN;    /* 首帧已带 6 字节数据 */
        tx_seq = 1;                        /* 第一个 CF 序号从 1 开始 */
        tx_state = ISO_TP_TX_WAIT_FC;
        tx_timeout_ms = get_time_ms() + ISO_TP_DEFAULT_TIMEOUT;

        printf("[ISO_TP] TX FF: id=0x%X, total_len=%lu, sent=%lu\n",
               can_id, (unsigned long)len, (unsigned long)tx_sent);
        return 0;  /* 还没发完 */
    }
}

int isotp_receive(uint32_t *can_id, uint8_t *buf, uint32_t buf_size)
{
    if (rx_state != ISO_TP_RX_IDLE) {
        /* 还在接收中，还没到完整的报文 */
        return 0;
    }

    if (rx_total_len == 0) {
        /* 没有可用的完整报文 */
        return 0;
    }

    /* 检查缓冲区大小 */
    if (buf_size < rx_total_len) {
        printf("[ISO_TP] RX buffer too small (%lu < %lu)\n",
               (unsigned long)buf_size, (unsigned long)rx_total_len);
        rx_total_len = 0;
        return -1;
    }

    /* 拷贝完整报文给调用者 */
    if (can_id)
        *can_id = rx_id;

    memcpy(buf, rx_buffer, rx_total_len);
    uint32_t received = rx_total_len;

    /* 清除接收状态 */
    rx_total_len = 0;

    return (int)received;
}

void isotp_poll(void)
{
    struct can_frame frame;
    long long now = get_time_ms();

    if (g_fd < 0)
        return;

    /* ========== 检查发送超时 ========== */
    if (tx_state != ISO_TP_TX_IDLE && now > tx_timeout_ms) {
        printf("[ISO_TP] TX timeout!\n");
        tx_state = ISO_TP_TX_IDLE;
    }

    /* ========== 检查接收超时 ========== */
    if (rx_state != ISO_TP_RX_IDLE && now > rx_timeout_ms) {
        printf("[ISO_TP] RX timeout! (expected seq=%d, got %lu/%lu bytes)\n",
               rx_exp_seq, (unsigned long)rx_received,
               (unsigned long)rx_total_len);
        rx_state = ISO_TP_RX_IDLE;
        rx_total_len = 0;
    }

    /* ========== 接收 CAN 帧 ========== */
    int ret = can_recv(g_fd, &frame);
    if (ret <= 0)
        return;

    uint8_t pci_type = get_pci_type(frame.data[0]);

    switch (pci_type) {

    /* ---- 收到单帧 (SF) ---- */
    case ISO_TP_PCI_SF: {
        uint32_t sf_len = get_frame_length(&frame);
        if (sf_len > ISO_TP_SF_MAX_LEN)
            break;

        rx_id = frame.can_id;
        memcpy(rx_buffer, &frame.data[1], sf_len);
        rx_total_len = sf_len;

        printf("[ISO_TP] RX SF: id=0x%X, len=%lu\n",
               frame.can_id, (unsigned long)sf_len);
        break;
    }

    /* ---- 收到首帧 (FF) — 准备多帧接收 ---- */
    case ISO_TP_PCI_FF: {
        uint32_t ff_len = get_frame_length(&frame);
        if (ff_len > ISO_TP_MAX_LEN) {
            printf("[ISO_TP] RX FF: length too large: %lu\n",
                   (unsigned long)ff_len);
            break;
        }

        rx_id = frame.can_id;
        rx_total_len = ff_len;

        /* 保存首帧中的前 6 字节 */
        memcpy(rx_buffer, &frame.data[2], ISO_TP_FF_DATA_LEN);
        rx_received = ISO_TP_FF_DATA_LEN;
        rx_exp_seq = 1;
        rx_state = ISO_TP_RX_WAITING_CF;

        /* 发送流控帧 (FC) */
        struct can_frame fc_frame;
        build_fc(&fc_frame, frame.can_id, ISO_TP_FC_CTS, 0, 10);
        can_send(g_fd, &fc_frame);

        rx_timeout_ms = get_time_ms() + ISO_TP_DEFAULT_TIMEOUT;

        printf("[ISO_TP] RX FF: id=0x%X, total=%lu, rx=%lu\n",
               frame.can_id, (unsigned long)ff_len,
               (unsigned long)rx_received);
        break;
    }

    /* ---- 收到连续帧 (CF) — 拼接收 ---- */
    case ISO_TP_PCI_CF: {
        if (rx_state != ISO_TP_RX_WAITING_CF)
            break;

        uint8_t seq = frame.data[0] & 0x0F;

        if (seq != rx_exp_seq) {
            printf("[ISO_TP] RX CF: seq mismatch (expect %d, got %d)\n",
                   rx_exp_seq, seq);
            rx_state = ISO_TP_RX_IDLE;
            rx_total_len = 0;
            break;
        }

        /* 本帧数据 */
        uint32_t cf_len = frame.can_dlc - 1;
        uint32_t remaining = rx_total_len - rx_received;
        if (cf_len > remaining)
            cf_len = remaining;

        memcpy(rx_buffer + rx_received, &frame.data[1], cf_len);
        rx_received += cf_len;

        /* 更新序号 */
        rx_exp_seq = (seq + 1) & 0x0F;

        /* 重置超时 */
        rx_timeout_ms = get_time_ms() + ISO_TP_DEFAULT_TIMEOUT;

        printf("[ISO_TP] RX CF: seq=%d, cf_len=%lu, total_rx=%lu/%lu\n",
               seq, (unsigned long)cf_len,
               (unsigned long)rx_received, (unsigned long)rx_total_len);

        /* 检查是否收完 */
        if (rx_received >= rx_total_len) {
            printf("[ISO_TP] RX complete: id=0x%X, len=%lu\n",
                   rx_id, (unsigned long)rx_total_len);
            rx_state = ISO_TP_RX_IDLE;
            /* 数据在 rx_buffer 中，调用者通过 isotp_receive 获取 */
        }
        break;
    }

    /* ---- 收到流控帧 (FC) — 发送方继续发 CF ---- */
    case ISO_TP_PCI_FC: {
        if (tx_state != ISO_TP_TX_WAIT_FC)
            break;

        uint8_t fc_status = frame.data[0] & 0x0F;

        if (fc_status != ISO_TP_FC_CTS) {
            printf("[ISO_TP] TX FC: unexpected status=%d, abort\n", fc_status);
            tx_state = ISO_TP_TX_IDLE;
            break;
        }

        /* 收到 FC 确认，开始发连续帧直到发完 */
        tx_state = ISO_TP_TX_SENDING_CF;
        tx_timeout_ms = get_time_ms() + ISO_TP_DEFAULT_TIMEOUT;

        /* fall through: 立即发送第一个 CF */
        /* (无 break，直接进入 CF 发送逻辑) */
        /* 但用 goto 或循环更清晰，此处直接发送一帧 */
        {
            struct can_frame cf_frame;
            uint32_t remaining = tx_total_len - tx_sent;
            uint32_t cf_len = (remaining > ISO_TP_CF_DATA_LEN)
                              ? ISO_TP_CF_DATA_LEN : remaining;

            build_cf(&cf_frame, tx_id, tx_seq,
                      tx_buffer + tx_sent, cf_len);
            can_send(g_fd, &cf_frame);

            printf("[ISO_TP] TX CF: seq=%d, len=%lu, remaining=%lu\n",
                   tx_seq, (unsigned long)cf_len, (unsigned long)remaining);

            tx_sent += cf_len;

            if (tx_sent >= tx_total_len) {
                /* 发完了 */
                tx_state = ISO_TP_TX_IDLE;
                printf("[ISO_TP] TX complete: id=0x%X, total=%lu\n",
                       tx_id, (unsigned long)tx_total_len);
            } else {
                /* 还有要发的 */
                tx_seq = (tx_seq + 1) & 0x0F;
                tx_state = ISO_TP_TX_SENDING_CF;
                tx_timeout_ms = get_time_ms() + ISO_TP_DEFAULT_TIMEOUT;
            }
        }
        break;
    }

    default:
        printf("[ISO_TP] Unknown PCI type: 0x%02X\n", frame.data[0]);
        break;
    }
}

int isotp_tx_idle(void)
{
    return (tx_state == ISO_TP_TX_IDLE) ? 1 : 0;
}

/**
 * 从外部送入一帧 CAN 数据供 ISO-TP 处理
 *
 * 当 RTE/CanIf 已经通过 can_recv 收到了一帧 CAN 数据，
 * 并且识别出这是诊断报文 ID 时，调用此函数将帧交给 ISO-TP。
 * 这样避免了多个模块同时 poll 同一个 fd 导致数据丢失。
 */
void isotp_rx_frame(const struct can_frame *frame)
{
    if (!frame || g_fd < 0)
        return;

    uint8_t pci_type = get_pci_type(frame->data[0]);
    long long now = get_time_ms();

    /* 检查发送超时（对于 FC 帧，发送方需要） */
    if (tx_state != ISO_TP_TX_IDLE && now > tx_timeout_ms) {
        printf("[ISO_TP] TX timeout!\n");
        tx_state = ISO_TP_TX_IDLE;
    }

    /* 检查接收超时 */
    if (rx_state != ISO_TP_RX_IDLE && now > rx_timeout_ms) {
        printf("[ISO_TP] RX timeout! (expected seq=%d, got %lu/%lu bytes)\n",
               rx_exp_seq, (unsigned long)rx_received,
               (unsigned long)rx_total_len);
        rx_state = ISO_TP_RX_IDLE;
        rx_total_len = 0;
    }

    switch (pci_type) {

    case ISO_TP_PCI_SF: {
        uint32_t sf_len = frame->data[0] & 0x0F;
        if (sf_len > ISO_TP_SF_MAX_LEN)
            break;
        rx_id = frame->can_id;
        memcpy(rx_buffer, &frame->data[1], sf_len);
        rx_total_len = sf_len;
        printf("[ISO_TP] RX SF: id=0x%X, len=%lu\n",
               frame->can_id, (unsigned long)sf_len);
        break;
    }

    case ISO_TP_PCI_FF: {
        uint32_t ff_len = get_frame_length(frame);
        if (ff_len > ISO_TP_MAX_LEN) {
            printf("[ISO_TP] RX FF: length too large: %lu\n",
                   (unsigned long)ff_len);
            break;
        }
        rx_id = frame->can_id;
        rx_total_len = ff_len;
        memcpy(rx_buffer, &frame->data[2], ISO_TP_FF_DATA_LEN);
        rx_received = ISO_TP_FF_DATA_LEN;
        rx_exp_seq = 1;
        rx_state = ISO_TP_RX_WAITING_CF;
        /* 发送流控帧 */
        struct can_frame fc_frame;
        build_fc(&fc_frame, frame->can_id, ISO_TP_FC_CTS, 0, 10);
        can_send(g_fd, &fc_frame);
        rx_timeout_ms = get_time_ms() + ISO_TP_DEFAULT_TIMEOUT;
        printf("[ISO_TP] RX FF: id=0x%X, total=%lu, rx=%lu\n",
               frame->can_id, (unsigned long)ff_len,
               (unsigned long)rx_received);
        break;
    }

    case ISO_TP_PCI_CF: {
        if (rx_state != ISO_TP_RX_WAITING_CF)
            break;
        uint8_t seq = frame->data[0] & 0x0F;
        if (seq != rx_exp_seq) {
            printf("[ISO_TP] RX CF: seq mismatch (expect %d, got %d)\n",
                   rx_exp_seq, seq);
            rx_state = ISO_TP_RX_IDLE;
            rx_total_len = 0;
            break;
        }
        uint32_t cf_len = frame->can_dlc - 1;
        uint32_t remaining = rx_total_len - rx_received;
        if (cf_len > remaining) cf_len = remaining;
        memcpy(rx_buffer + rx_received, &frame->data[1], cf_len);
        rx_received += cf_len;
        rx_exp_seq = (seq + 1) & 0x0F;
        rx_timeout_ms = get_time_ms() + ISO_TP_DEFAULT_TIMEOUT;
        printf("[ISO_TP] RX CF: seq=%d, cf_len=%lu, total_rx=%lu/%lu\n",
               seq, (unsigned long)cf_len,
               (unsigned long)rx_received, (unsigned long)rx_total_len);
        if (rx_received >= rx_total_len) {
            printf("[ISO_TP] RX complete: id=0x%X, len=%lu\n",
                   rx_id, (unsigned long)rx_total_len);
            rx_state = ISO_TP_RX_IDLE;
        }
        break;
    }

    case ISO_TP_PCI_FC: {
        if (tx_state != ISO_TP_TX_WAIT_FC)
            break;
        uint8_t fc_status = frame->data[0] & 0x0F;
        if (fc_status != ISO_TP_FC_CTS) {
            printf("[ISO_TP] TX FC: unexpected status=%d, abort\n", fc_status);
            tx_state = ISO_TP_TX_IDLE;
            break;
        }
        tx_state = ISO_TP_TX_SENDING_CF;
        tx_timeout_ms = get_time_ms() + ISO_TP_DEFAULT_TIMEOUT;
        {
            struct can_frame cf_frame;
            uint32_t remaining = tx_total_len - tx_sent;
            uint32_t cf_len = (remaining > ISO_TP_CF_DATA_LEN)
                              ? ISO_TP_CF_DATA_LEN : remaining;
            build_cf(&cf_frame, tx_id, tx_seq,
                      tx_buffer + tx_sent, cf_len);
            can_send(g_fd, &cf_frame);
            printf("[ISO_TP] TX CF: seq=%d, len=%lu, remaining=%lu\n",
                   tx_seq, (unsigned long)cf_len, (unsigned long)remaining);
            tx_sent += cf_len;
            if (tx_sent >= tx_total_len) {
                tx_state = ISO_TP_TX_IDLE;
                printf("[ISO_TP] TX complete: id=0x%X, total=%lu\n",
                       tx_id, (unsigned long)tx_total_len);
            } else {
                tx_seq = (tx_seq + 1) & 0x0F;
                tx_state = ISO_TP_TX_SENDING_CF;
                tx_timeout_ms = get_time_ms() + ISO_TP_DEFAULT_TIMEOUT;
            }
        }
        break;
    }

    default:
        printf("[ISO_TP] Unknown PCI type: 0x%02X\n", frame->data[0]);
        break;
    }
}
