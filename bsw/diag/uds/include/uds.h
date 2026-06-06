#ifndef UDS_H
#define UDS_H

/**
 * @file uds.h
 * @brief UDS (Unified Diagnostic Services) — ISO 14229
 *
 * UDS 是汽车行业标准诊断协议，定义诊断仪(Tester)与 ECU 之间的通信规范。
 * 本实现覆盖最常用的 6 个服务：
 *   0x10 — DiagSessionControl    诊断会话控制
 *   0x22 — ReadDataByIdentifier  读取数据
 *   0x2E — WriteDataByIdentifier 写入数据
 *   0x27 — SecurityAccess        安全访问（种子-密钥）
 *   0x19 — ReadDTCInformation    读取故障码
 *   0x14 — ClearDiagnosticInformation 清除故障码
 *
 * 通信方式：UDS 请求通过 ISO-TP 传输层收发。
 *
 * 使用示例：
 * @code
 *   uds_init(request_can_id, response_can_id);
 *
 *   while (1) {
 *       isotp_poll();
 *
 *       // 检查是否有新的 UDS 请求
 *       if (isotp_receive(NULL, request_buf, sizeof(request_buf)) > 0) {
 *           uds_response_t resp;
 *           uds_handle_request(request_buf, request_len, &resp);
 *           isotp_send(uds_get_response_id(), resp.data, resp.len);
 *       }
 *   }
 * @endcode
 */

#include <stdint.h>

/* ==================== SID 定义 (Service IDs) ==================== */

/* 诊断会话控制 */
#define UDS_SID_DIAG_SESSION_CONTROL       0x10

/* 读取数据 */
#define UDS_SID_READ_DATA_BY_ID            0x22

/* 写入数据 */
#define UDS_SID_WRITE_DATA_BY_ID           0x2E

/* 安全访问 */
#define UDS_SID_SECURITY_ACCESS            0x27

/* 读取故障码 */
#define UDS_SID_READ_DTC_INFO              0x19

/* 清除故障码 */
#define UDS_SID_CLEAR_DTC                  0x14

/* 否定响应 SID (固定值) */
#define UDS_SID_NEGATIVE_RESPONSE          0x7F

/* ==================== 会话类型 (0x10 子功能) ==================== */

#define UDS_SESSION_DEFAULT                0x01  /* 默认会话 */
#define UDS_SESSION_PROGRAMMING            0x02  /* 编程会话 */
#define UDS_SESSION_EXTENDED               0x03  /* 扩展会话 */

/* ==================== 安全访问子功能 (0x27) ==================== */

#define UDS_SECURITY_REQUEST_SEED          0x01  /* 请求种子 */
#define UDS_SECURITY_SEND_KEY              0x02  /* 发送密钥 */

/* ==================== DTC 子功能 (0x19) ==================== */

#define UDS_DTC_REPORT_NUMBER              0x01  /* 报告故障码数量 */
#define UDS_DTC_REPORT_BY_STATUS           0x02  /* 按状态报告故障码 */

/* ==================== NRC 定义 (Negative Response Codes) ==================== */

#define UDS_NRC_GRANTED                    0x00  /* 无错误 (内部使用) */
#define UDS_NRC_WRONG_MESSAGE_LENGTH       0x13  /* 报文长度错误 */
#define UDS_NRC_CONDITIONS_NOT_CORRECT     0x22  /* 条件不满足 */
#define UDS_NRC_REQUEST_SEQUENCE_ERROR     0x24  /* 请求顺序错误 */
#define UDS_NRC_REQUEST_OUT_OF_RANGE       0x31  /* 参数超出范围 */
#define UDS_NRC_SECURITY_ACCESS_DENIED     0x33  /* 安全访问被拒 */
#define UDS_NRC_INVALID_KEY                0x35  /* 密钥无效 */
#define UDS_NRC_EXCEEDED_NUM_ATTEMPTS      0x36  /* 超过尝试次数 */
#define UDS_NRC_REQUIRED_TIME_DELAY        0x37  /* 需要等待 */
#define UDS_NRC_SERVICE_NOT_SUPPORTED      0x11  /* 服务不支持 */
#define UDS_NRC_SUBFUNCTION_NOT_SUPPORTED  0x12  /* 子功能不支持 */
#define UDS_NRC_GENERAL_REJECT             0x10  /* 一般性拒绝 */

/* ==================== DID 定义 (Data Identifiers) ==================== */

/* 车辆数据 DID — 通过这些 ID 读写车辆参数 */
#define UDS_DID_ENGINE_SPEED               0xF100  /* 发动机转速 */
#define UDS_DID_VEHICLE_SPEED              0xF101  /* 车速 */
#define UDS_DID_ENGINE_TEMP                0xF102  /* 发动机温度 */
#define UDS_DID_VIN                        0xF190  /* 车辆识别码 (VIN) */
#define UDS_DID_SW_VERSION                 0xF195  /* 软件版本号 */

/* ==================== 数据结构 ==================== */

/** UDS 会话状态 */
typedef enum {
    UDS_SESSION_DEFAULT_ACTIVE = 0,   /* 默认会话中 */
    UDS_SESSION_PROGRAMMING_ACTIVE,   /* 编程会话中 */
    UDS_SESSION_EXTENDED_ACTIVE,      /* 扩展会话中 */
} uds_session_t;

/** 安全访问状态 */
typedef enum {
    UDS_SECURITY_LOCKED = 0,          /* 锁定 */
    UDS_SECURITY_UNLOCKED,            /* 已解锁 */
} uds_security_t;

/** UDS 响应结构 */
typedef struct {
    uint8_t  data[4095];              /* 响应数据缓冲区 */
    uint16_t len;                     /* 响应长度 */
} uds_response_t;

/* ==================== 外部接口 ==================== */

/**
 * @brief 初始化 UDS 层
 *
 * @param request_can_id  接收诊断请求的 CAN ID（通常 0x7E0）
 * @param response_can_id 发送诊断响应的 CAN ID（通常 0x7E8）
 */
void uds_init(uint32_t request_can_id, uint32_t response_can_id);

/**
 * @brief 处理一个 UDS 请求
 *
 * 这是 UDS 协议栈的核心函数。输入请求报文，输出响应报文。
 *
 * @param[in]  request   请求报文数据
 * @param[in]  req_len   请求长度
 * @param[out] response  响应结构（包含数据+长度）
 *
 * @return 0  正常处理完成（肯定或否定响应都在 response 里）
 *         -1 请求报文格式错误
 */
int uds_handle_request(const uint8_t *request, uint16_t req_len,
                        uds_response_t *response);

/**
 * @brief 获取当前会话类型
 */
uds_session_t uds_get_session(void);

/**
 * @brief 获取安全访问状态
 */
uds_security_t uds_get_security(void);

/**
 * @brief 获取诊断请求接收的 CAN ID
 */
uint32_t uds_get_request_id(void);

/**
 * @brief 获取诊断响应发送的 CAN ID
 */
uint32_t uds_get_response_id(void);

/**
 * @brief 注册外部 DID 读取回调（让应用层 SWC 提供数据）
 *
 * 当收到 0x22 读取某个 DID 时，如果内部没有该 DID 的数据，
 * 会调用此回调让应用层提供。
 *
 * @param callback 回调函数: (did, data, max_len) → 返回实际数据长度, -1=不支持
 */
void uds_register_read_callback(int (*callback)(uint16_t did,
                                                 uint8_t *data,
                                                 uint16_t max_len));

/**
 * @brief 注册外部 DID 写入回调（让应用层 SWC 处理写入）
 *
 * @param callback 回调函数: (did, data, len) → 0=成功, -1=失败
 */
void uds_register_write_callback(int (*callback)(uint16_t did,
                                                  const uint8_t *data,
                                                  uint16_t len));

#endif /* UDS_H */
