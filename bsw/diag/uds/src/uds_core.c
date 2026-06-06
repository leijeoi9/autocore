#include "uds.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

/* ==================== 内部状态 ==================== */

static uint32_t g_request_id  = 0x7E0;   /* 诊断请求 CAN ID (Tester → ECU) */
static uint32_t g_response_id = 0x7E8;   /* 诊断响应 CAN ID (ECU → Tester) */

static uds_session_t  g_session  = UDS_SESSION_DEFAULT_ACTIVE;
static uds_security_t g_security = UDS_SECURITY_LOCKED;

/* 安全访问相关 */
static uint32_t g_seed;                   /* 当前种子 */
static int      g_seed_valid = 0;         /* 种子是否已生成 */
static int      g_access_attempts = 0;    /* 连续失败次数 */
#define MAX_ACCESS_ATTEMPTS 3
#define ACCESS_LOCKOUT_TIME_MS 10000      /* 超过尝试次数后锁定 10 秒 */
static long long g_lockout_until = 0;

/* VIN 码 */
static const char *g_vin = "AUTOCORE123456789";

/* 外部回调 */
static int (*g_read_cb)(uint16_t did, uint8_t *data, uint16_t max_len) = NULL;
static int (*g_write_cb)(uint16_t did, const uint8_t *data, uint16_t len) = NULL;

/* DTC 数据库 */
#define MAX_DTC 16

typedef struct {
    uint32_t dtc;       /* 3 字节故障码 (如 0x123456) */
    uint8_t  status;    /* DTC 状态掩码 */
    char     desc[64];  /* 描述 */
} dtc_entry_t;

static dtc_entry_t g_dtc_db[MAX_DTC];
static int g_dtc_count = 0;

/* ==================== 辅助函数 ==================== */

/** 构建否定响应 */
static void build_negative_response(uint8_t sid, uint8_t nrc,
                                     uds_response_t *resp)
{
    resp->data[0] = UDS_SID_NEGATIVE_RESPONSE;  /* 0x7F */
    resp->data[1] = sid;
    resp->data[2] = nrc;
    resp->len = 3;

    printf("[UDS] NRC: SID=0x%02X, NRC=0x%02X (%s)\n", sid, nrc,
           nrc == UDS_NRC_SERVICE_NOT_SUPPORTED ? "service not supported" :
           nrc == UDS_NRC_SUBFUNCTION_NOT_SUPPORTED ? "subfunction not supported" :
           nrc == UDS_NRC_WRONG_MESSAGE_LENGTH ? "wrong length" :
           nrc == UDS_NRC_CONDITIONS_NOT_CORRECT ? "conditions wrong" :
           nrc == UDS_NRC_REQUEST_OUT_OF_RANGE ? "out of range" :
           nrc == UDS_NRC_SECURITY_ACCESS_DENIED ? "security denied" :
           "unknown");
}

/** 构建肯定响应 */
static void build_positive_response(uint8_t sid, const uint8_t *data,
                                     uint16_t data_len, uds_response_t *resp)
{
    resp->data[0] = sid + 0x40;  /* 肯定响应 = SID + 0x40 */
    if (data && data_len > 0)
        memcpy(&resp->data[1], data, data_len);
    resp->len = 1 + data_len;
}

/** 获取当前时间 (ms) */
static long long get_time_ms_uds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* ==================== 各个服务的处理函数 ==================== */

/**
 * 0x10 — DiagSessionControl
 *
 * 请求格式: [0x10] [子功能]
 *   0x01 = 默认会话 (DefaultSession)
 *   0x02 = 编程会话 (ProgrammingSession)
 *   0x03 = 扩展会话 (ExtendedSession)
 *
 * 响应格式: [0x50] [子功能]
 */
static void handle_session_control(const uint8_t *req, uint16_t req_len,
                                    uds_response_t *resp)
{
    if (req_len < 2) {
        build_negative_response(UDS_SID_DIAG_SESSION_CONTROL,
                                 UDS_NRC_WRONG_MESSAGE_LENGTH, resp);
        return;
    }

    uint8_t subfunc = req[1];

    switch (subfunc) {
    case UDS_SESSION_DEFAULT:
        g_session = UDS_SESSION_DEFAULT_ACTIVE;
        g_security = UDS_SECURITY_LOCKED;   /* 切换会话会锁定安全访问 */
        printf("[UDS] Session: DEFAULT\n");
        break;

    case UDS_SESSION_PROGRAMMING:
        g_session = UDS_SESSION_PROGRAMMING_ACTIVE;
        g_security = UDS_SECURITY_LOCKED;
        printf("[UDS] Session: PROGRAMMING\n");
        break;

    case UDS_SESSION_EXTENDED:
        g_session = UDS_SESSION_EXTENDED_ACTIVE;
        g_security = UDS_SECURITY_LOCKED;
        printf("[UDS] Session: EXTENDED\n");
        break;

    default:
        build_negative_response(UDS_SID_DIAG_SESSION_CONTROL,
                                 UDS_NRC_SUBFUNCTION_NOT_SUPPORTED, resp);
        return;
    }

    /* 肯定响应 */
    build_positive_response(UDS_SID_DIAG_SESSION_CONTROL,
                             &subfunc, 1, resp);
}

/**
 * 0x22 — ReadDataByIdentifier
 *
 * 请求格式: [0x22] [DID 高字节] [DID 低字节]
 * 响应格式: [0x62] [DID 高] [DID 低] [数据...]
 */
static void handle_read_data(const uint8_t *req, uint16_t req_len,
                              uds_response_t *resp)
{
    if (req_len < 3) {
        build_negative_response(UDS_SID_READ_DATA_BY_ID,
                                 UDS_NRC_WRONG_MESSAGE_LENGTH, resp);
        return;
    }

    uint16_t did = ((uint16_t)req[1] << 8) | req[2];

    uint8_t data[256];
    uint16_t data_len = 0;

    switch (did) {
    case UDS_DID_ENGINE_SPEED: {
        /* 这里应该从 RTE 读取实时值，演示用固定值 */
        float speed = 0.0f;
        /* 如果注册了外部回调，优先使用 */
        if (g_read_cb && g_read_cb(did, data, sizeof(data)) >= 0) {
            data_len = 4;
        } else {
            memcpy(data, &speed, sizeof(speed));
            data_len = sizeof(speed);
        }
        break;
    }

    case UDS_DID_VEHICLE_SPEED: {
        float speed = 0.0f;
        if (g_read_cb && g_read_cb(did, data, sizeof(data)) >= 0) {
            data_len = 4;
        } else {
            memcpy(data, &speed, sizeof(speed));
            data_len = sizeof(speed);
        }
        break;
    }

    case UDS_DID_ENGINE_TEMP: {
        float temp = 0.0f;
        if (g_read_cb && g_read_cb(did, data, sizeof(data)) >= 0) {
            data_len = 4;
        } else {
            memcpy(data, &temp, sizeof(temp));
            data_len = sizeof(temp);
        }
        break;
    }

    case UDS_DID_VIN: {
        uint16_t vin_len = (uint16_t)strlen(g_vin);
        memcpy(data, g_vin, vin_len);
        data_len = vin_len;
        break;
    }

    case UDS_DID_SW_VERSION: {
        const char *ver = "AutoCore v1.0.0";
        uint16_t ver_len = (uint16_t)strlen(ver);
        memcpy(data, ver, ver_len);
        data_len = ver_len;
        break;
    }

    default:
        /* 尝试外部回调 */
        if (g_read_cb && g_read_cb(did, data, sizeof(data)) >= 0) {
            data_len = 4;
        } else {
            build_negative_response(UDS_SID_READ_DATA_BY_ID,
                                     UDS_NRC_REQUEST_OUT_OF_RANGE, resp);
            return;
        }
        break;
    }

    /* 肯定响应: [0x62] [DID高] [DID低] [数据] */
    uint8_t resp_data[256];
    resp_data[0] = (did >> 8) & 0xFF;
    resp_data[1] = did & 0xFF;
    memcpy(&resp_data[2], data, data_len);

    build_positive_response(UDS_SID_READ_DATA_BY_ID, resp_data, data_len + 2, resp);

    printf("[UDS] ReadData: DID=0x%04X, len=%u\n", did, data_len);
}

/**
 * 0x2E — WriteDataByIdentifier
 *
 * 请求格式: [0x2E] [DID高] [DID低] [数据...]
 * 响应格式: [0x6E] [DID高] [DID低]
 */
static void handle_write_data(const uint8_t *req, uint16_t req_len,
                               uds_response_t *resp)
{
    if (req_len < 3) {
        build_negative_response(UDS_SID_WRITE_DATA_BY_ID,
                                 UDS_NRC_WRONG_MESSAGE_LENGTH, resp);
        return;
    }

    /* 安全检查：扩展或编程会话才允许写入 */
    if (g_session == UDS_SESSION_DEFAULT_ACTIVE) {
        build_negative_response(UDS_SID_WRITE_DATA_BY_ID,
                                 UDS_NRC_CONDITIONS_NOT_CORRECT, resp);
        return;
    }

    uint16_t did = ((uint16_t)req[1] << 8) | req[2];
    uint16_t data_len = req_len - 3;
    const uint8_t *data = &req[3];

    int handled = 0;

    switch (did) {
    case UDS_DID_VIN:
        if (data_len > 17) {
            build_negative_response(UDS_SID_WRITE_DATA_BY_ID,
                                     UDS_NRC_WRONG_MESSAGE_LENGTH, resp);
            return;
        }
        /* 实际项目中 VIN 写入应持久化到 NVRAM */
        printf("[UDS] WriteVIN: would write %u bytes\n", data_len);
        handled = 1;
        break;

    default:
        /* 尝试外部回调 */
        if (g_write_cb && g_write_cb(did, data, data_len) == 0) {
            handled = 1;
        }
        break;
    }

    if (!handled) {
        build_negative_response(UDS_SID_WRITE_DATA_BY_ID,
                                 UDS_NRC_REQUEST_OUT_OF_RANGE, resp);
        return;
    }

    /* 肯定响应: [0x6E] [DID高] [DID低] */
    uint8_t resp_data[2] = {(did >> 8) & 0xFF, did & 0xFF};
    build_positive_response(UDS_SID_WRITE_DATA_BY_ID, resp_data, 2, resp);

    printf("[UDS] WriteData: DID=0x%04X, len=%u\n", did, data_len);
}

/**
 * 0x27 — SecurityAccess
 *
 * 两步验证：
 *   请求种子: [0x27] [0x01]               → 响应: [0x67] [0x01] [种子数据]
 *   发送密钥: [0x27] [0x02] [密钥数据]     → 响应: [0x67] [0x02]
 */
static void handle_security_access(const uint8_t *req, uint16_t req_len,
                                    uds_response_t *resp)
{
    if (req_len < 2) {
        build_negative_response(UDS_SID_SECURITY_ACCESS,
                                 UDS_NRC_WRONG_MESSAGE_LENGTH, resp);
        return;
    }

    uint8_t subfunc = req[1];
    long long now = get_time_ms_uds();

    /* 检查是否在锁定期 */
    if (now < g_lockout_until) {
        build_negative_response(UDS_SID_SECURITY_ACCESS,
                                 UDS_NRC_EXCEEDED_NUM_ATTEMPTS, resp);
        return;
    }

    if (subfunc == UDS_SECURITY_REQUEST_SEED) {
        /* ---- 第 1 步：请求种子 ---- */
        /* 生成随机种子（演示用固定值） */
        g_seed = 0xA5A5A5A5 ^ (uint32_t)(now & 0xFFFF);
        g_seed_valid = 1;
        g_access_attempts = 0;

        /* 响应: [0x67] [0x01] [4字节种子] */
        uint8_t resp_data[5];
        resp_data[0] = subfunc;
        resp_data[1] = (g_seed >> 24) & 0xFF;
        resp_data[2] = (g_seed >> 16) & 0xFF;
        resp_data[3] = (g_seed >> 8) & 0xFF;
        resp_data[4] = g_seed & 0xFF;

        build_positive_response(UDS_SID_SECURITY_ACCESS, resp_data, 5, resp);
        printf("[UDS] Security: seed=0x%08X\n", g_seed);

    } else if (subfunc == UDS_SECURITY_SEND_KEY) {
        /* ---- 第 2 步：发送密钥 ---- */
        if (!g_seed_valid) {
            build_negative_response(UDS_SID_SECURITY_ACCESS,
                                     UDS_NRC_REQUEST_SEQUENCE_ERROR, resp);
            return;
        }

        if (req_len < 6) {
            build_negative_response(UDS_SID_SECURITY_ACCESS,
                                     UDS_NRC_WRONG_MESSAGE_LENGTH, resp);
            return;
        }

        /* 解析客户端发来的密钥 */
        uint32_t key = ((uint32_t)req[2] << 24) |
                       ((uint32_t)req[3] << 16) |
                       ((uint32_t)req[4] << 8)  |
                       ((uint32_t)req[5]);

        /* 验证：简单的算法 key = seed ^ 0x12345678 */
        uint32_t expected_key = g_seed ^ 0x12345678;

        if (key == expected_key) {
            g_security = UDS_SECURITY_UNLOCKED;
            g_seed_valid = 0;
            g_access_attempts = 0;

            uint8_t resp_data[1] = {subfunc};
            build_positive_response(UDS_SID_SECURITY_ACCESS, resp_data, 1, resp);

            printf("[UDS] Security: UNLOCKED\n");
        } else {
            g_access_attempts++;

            if (g_access_attempts >= MAX_ACCESS_ATTEMPTS) {
                g_lockout_until = now + ACCESS_LOCKOUT_TIME_MS;
                printf("[UDS] Security: too many attempts, locked for %d ms\n",
                       ACCESS_LOCKOUT_TIME_MS);
            }

            g_seed_valid = 0;
            build_negative_response(UDS_SID_SECURITY_ACCESS,
                                     UDS_NRC_INVALID_KEY, resp);
            printf("[UDS] Security: INVALID KEY (got 0x%08X, expected 0x%08X)\n",
                   key, expected_key);
        }
    } else {
        build_negative_response(UDS_SID_SECURITY_ACCESS,
                                 UDS_NRC_SUBFUNCTION_NOT_SUPPORTED, resp);
    }
}

/**
 * 0x19 — ReadDTCInformation
 *
 * 请求格式: [0x19] [子功能]
 *   0x01 = 报告故障码数量
 *   0x02 = 按状态报告故障码
 */
static void handle_read_dtc(const uint8_t *req, uint16_t req_len,
                             uds_response_t *resp)
{
    if (req_len < 2) {
        build_negative_response(UDS_SID_READ_DTC_INFO,
                                 UDS_NRC_WRONG_MESSAGE_LENGTH, resp);
        return;
    }

    uint8_t subfunc = req[1];

    switch (subfunc) {
    case UDS_DTC_REPORT_NUMBER: {
        /* 报告故障码数量 */
        uint8_t resp_data[5];
        resp_data[0] = subfunc;
        resp_data[1] = 0;  /* 格式字节 */
        resp_data[2] = 0;  /* DTC 数量高字节 */
        resp_data[3] = g_dtc_count;  /* DTC 数量低字节 */
        resp_data[4] = 0;  /* 严重程度 */

        build_positive_response(UDS_SID_READ_DTC_INFO, resp_data, 5, resp);
        printf("[UDS] DTC count: %d\n", g_dtc_count);
        break;
    }

    case UDS_DTC_REPORT_BY_STATUS: {
        /* 按状态报告 DTC 列表 */
        uint8_t status_mask = (req_len >= 3) ? req[2] : 0xFF;

        uint8_t resp_data[256];
        uint16_t offset = 0;

        resp_data[offset++] = subfunc;
        resp_data[offset++] = 0;  /* 格式字节 */

        for (int i = 0; i < g_dtc_count; i++) {
            if (g_dtc_db[i].status & status_mask) {
                resp_data[offset++] = (g_dtc_db[i].dtc >> 16) & 0xFF;
                resp_data[offset++] = (g_dtc_db[i].dtc >> 8) & 0xFF;
                resp_data[offset++] = g_dtc_db[i].dtc & 0xFF;
                resp_data[offset++] = g_dtc_db[i].status;
            }
        }

        build_positive_response(UDS_SID_READ_DTC_INFO, resp_data, offset, resp);
        printf("[UDS] DTC list reported: %u entries\n", offset / 4);
        break;
    }

    default:
        build_negative_response(UDS_SID_READ_DTC_INFO,
                                 UDS_NRC_SUBFUNCTION_NOT_SUPPORTED, resp);
        break;
    }
}

/**
 * 0x14 — ClearDiagnosticInformation
 *
 * 请求格式: [0x14] [0xFF] [0xFF] [0xFF] (清除所有 DTC)
 * 响应格式: [0x54]
 */
static void handle_clear_dtc(const uint8_t *req, uint16_t req_len,
                              uds_response_t *resp)
{
    (void)req;

    if (req_len < 4) {
        build_negative_response(UDS_SID_CLEAR_DTC,
                                 UDS_NRC_WRONG_MESSAGE_LENGTH, resp);
        return;
    }

    /* 清除所有 DTC */
    g_dtc_count = 0;
    memset(g_dtc_db, 0, sizeof(g_dtc_db));

    build_positive_response(UDS_SID_CLEAR_DTC, NULL, 0, resp);
    printf("[UDS] DTC cleared\n");
}

/* ==================== SID 分发表 ==================== */

typedef struct {
    uint8_t sid;
    void (*handler)(const uint8_t *req, uint16_t req_len,
                     uds_response_t *resp);
} uds_service_t;

static const uds_service_t uds_services[] = {
    {UDS_SID_DIAG_SESSION_CONTROL,    handle_session_control},
    {UDS_SID_READ_DATA_BY_ID,         handle_read_data},
    {UDS_SID_WRITE_DATA_BY_ID,        handle_write_data},
    {UDS_SID_SECURITY_ACCESS,         handle_security_access},
    {UDS_SID_READ_DTC_INFO,           handle_read_dtc},
    {UDS_SID_CLEAR_DTC,               handle_clear_dtc},
};

#define UDS_SERVICE_COUNT (sizeof(uds_services) / sizeof(uds_services[0]))

/* ==================== 公开接口实现 ==================== */

void uds_init(uint32_t request_can_id, uint32_t response_can_id)
{
    g_request_id  = request_can_id;
    g_response_id = response_can_id;
    g_session  = UDS_SESSION_DEFAULT_ACTIVE;
    g_security = UDS_SECURITY_LOCKED;
    g_dtc_count = 0;
    memset(g_dtc_db, 0, sizeof(g_dtc_db));

    printf("[UDS] Initialized (req=0x%X, resp=0x%X)\n",
           g_request_id, g_response_id);
}

int uds_handle_request(const uint8_t *request, uint16_t req_len,
                        uds_response_t *response)
{
    if (!request || req_len < 1 || !response)
        return -1;

    uint8_t sid = request[0];

    printf("[UDS] Request: SID=0x%02X, len=%u\n", sid, req_len);

    /* 查找对应的服务处理函数 */
    for (size_t i = 0; i < UDS_SERVICE_COUNT; i++) {
        if (uds_services[i].sid == sid) {
            uds_services[i].handler(request, req_len, response);
            printf("[UDS] Response: len=%u\n", response->len);
            return 0;
        }
    }

    /* 没有匹配的 SID */
    build_negative_response(sid, UDS_NRC_SERVICE_NOT_SUPPORTED, response);
    printf("[UDS] SID 0x%02X not supported\n", sid);
    return 0;
}

/* ==================== 获取状态 ==================== */

uds_session_t  uds_get_session(void)       { return g_session; }
uds_security_t uds_get_security(void)      { return g_security; }
uint32_t uds_get_request_id(void)          { return g_request_id; }
uint32_t uds_get_response_id(void)         { return g_response_id; }

void uds_register_read_callback(int (*callback)(uint16_t did,
                                                  uint8_t *data,
                                                  uint16_t max_len))
{
    g_read_cb = callback;
}

void uds_register_write_callback(int (*callback)(uint16_t did,
                                                   const uint8_t *data,
                                                   uint16_t len))
{
    g_write_cb = callback;
}

/* ==================== DTC 管理 (供外部调用) ==================== */

int uds_dtc_set(uint32_t dtc, uint8_t status, const char *desc)
{
    if (g_dtc_count >= MAX_DTC)
        return -1;

    /* 检查是否已存在，存在则更新状态 */
    for (int i = 0; i < g_dtc_count; i++) {
        if (g_dtc_db[i].dtc == dtc) {
            g_dtc_db[i].status = status;
            return 0;
        }
    }

    g_dtc_db[g_dtc_count].dtc = dtc;
    g_dtc_db[g_dtc_count].status = status;
    if (desc)
        strncpy(g_dtc_db[g_dtc_count].desc, desc, sizeof(g_dtc_db[g_dtc_count].desc) - 1);
    g_dtc_count++;

    printf("[UDS] DTC set: 0x%06X, status=0x%02X, desc=%s\n",
           dtc, status, desc ? desc : "");
    return 0;
}

int uds_dtc_clear(uint32_t dtc)
{
    for (int i = 0; i < g_dtc_count; i++) {
        if (g_dtc_db[i].dtc == dtc) {
            /* 用最后一个元素覆盖 */
            g_dtc_count--;
            g_dtc_db[i] = g_dtc_db[g_dtc_count];
            printf("[UDS] DTC cleared: 0x%06X\n", dtc);
            return 0;
        }
    }
    return -1;
}
