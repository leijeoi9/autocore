#!/usr/bin/env python3
"""
AutoCore DoIP 诊断测试工具

通过以太网 DoIP (ISO 13400) 协议对 AutoCore 网关进行 UDS 诊断测试。
不需要 CAN 硬件，只需要网络连接。

用法:
  # 基本用法（默认 192.168.1.100:13400）
  python3 doip_diag.py

  # 指定 IP
  python3 doip_diag.py 192.168.1.101

  # 交互模式
  python3 doip_diag.py --interactive
"""

import socket
import struct
import sys
import argparse

# ==================== DoIP 协议常量 ====================
DOIP_PROTOCOL_VERSION = 0x02
DOIP_INVERSE_VERSION  = 0xFD

# DoIP 负载类型
DOIP_GENERIC_DOIP_NACK           = 0x0000
DOIP_VEHICLE_ID_REQUEST          = 0x0001
DOIP_VEHICLE_ID_RESPONSE         = 0x0002
DOIP_ROUTING_ACTIVATION_REQUEST  = 0x0007
DOIP_ROUTING_ACTIVATION_RESPONSE = 0x0008
DOIP_DIAGNOSTIC_MESSAGE          = 0x8001
DOIP_DIAGNOSTIC_MESSAGE_ACK      = 0x8002

# DoIP NACK 码
DOIP_NACK_INVALID_VERSION       = 0x00
DOIP_NACK_UNKNOWN_PAYLOAD       = 0x02
DOIP_NACK_INVALID_LENGTH        = 0x04

# 路由激活响应码
DOIP_ROUTE_ACCEPTED             = 0x10
DOIP_ROUTE_DISALLOWED           = 0x11

# ==================== 逻辑地址 ====================
AUTOCORE_LOGICAL_ADDRESS = 0x0E80  # AutoCore ECU 逻辑地址
TESTER_LOGICAL_ADDRESS   = 0x0000  # 诊断仪逻辑地址

# ==================== UDS 协议常量 ====================
UDS_SID_NEGATIVE_RESPONSE = 0x7F

UDS_NRC_SERVICE_NOT_SUPPORTED     = 0x11
UDS_NRC_SUBFUNCTION_NOT_SUPPORTED = 0x12
UDS_NRC_WRONG_MESSAGE_LENGTH      = 0x13
UDS_NRC_CONDITIONS_NOT_CORRECT    = 0x22
UDS_NRC_REQUEST_OUT_OF_RANGE      = 0x31
UDS_NRC_SECURITY_ACCESS_DENIED    = 0x33
UDS_NRC_INVALID_KEY               = 0x35
UDS_NRC_EXCEEDED_NUM_ATTEMPTS     = 0x36

NRC_NAMES = {
    0x11: "Service Not Supported",
    0x12: "SubFunction Not Supported",
    0x13: "Wrong Message Length",
    0x22: "Conditions Not Correct",
    0x31: "Request Out Of Range",
    0x33: "Security Access Denied",
    0x35: "Invalid Key",
    0x36: "Exceeded Number Of Attempts",
}


class DoIPClient:
    """DoIP 诊断客户端"""

    def __init__(self, host="192.168.1.100", port=13400, timeout=5):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.sock = None
        self.routing_active = False

    def connect(self):
        """建立 TCP 连接"""
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.settimeout(self.timeout)
        self.sock.connect((self.host, self.port))
        print(f"[DoIP] Connected to {self.host}:{self.port}")
        return True

    def close(self):
        """关闭连接"""
        if self.sock:
            self.sock.close()
            self.sock = None
        self.routing_active = False

    def _send_doip(self, payload_type, payload):
        """发送 DoIP 报文"""
        header = struct.pack("!BBHI",
                             DOIP_PROTOCOL_VERSION,
                             DOIP_INVERSE_VERSION,
                             payload_type,
                             len(payload))
        self.sock.sendall(header + payload)

    def _recv_doip(self):
        """接收 DoIP 报文，返回 (payload_type, payload)"""
        header = self.sock.recv(8)
        if len(header) < 8:
            raise ConnectionError("Connection closed")

        proto_ver, inv_ver, payload_type, payload_len = struct.unpack("!BBHI", header)
        payload = b""
        while len(payload) < payload_len:
            chunk = self.sock.recv(payload_len - len(payload))
            if not chunk:
                raise ConnectionError("Connection closed")
            payload += chunk
        return payload_type, payload

    def routing_activate(self, source_addr=TESTER_LOGICAL_ADDRESS,
                         target_addr=AUTOCORE_LOGICAL_ADDRESS):
        """路由激活"""
        payload = struct.pack("!HBBBBBB", source_addr, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00)
        self._send_doip(DOIP_ROUTING_ACTIVATION_REQUEST, payload)
        ptype, payload = self._recv_doip()

        if ptype == DOIP_ROUTING_ACTIVATION_RESPONSE:
            src = struct.unpack_from("!H", payload, 0)[0]
            tgt = struct.unpack_from("!H", payload, 2)[0]
            code = payload[4]
            if code == DOIP_ROUTE_ACCEPTED:
                self.routing_active = True
                print(f"[DoIP] Routing activated: src=0x{src:04X}, tgt=0x{tgt:04X}")
                return True
            else:
                print(f"[DoIP] Routing rejected: code=0x{code:02X}")
                return False
        elif ptype == DOIP_GENERIC_DOIP_NACK:
            print(f"[DoIP] NACK: {payload[0]}")
            return False
        return False

    def send_uds(self, uds_data, source_addr=TESTER_LOGICAL_ADDRESS,
                 target_addr=AUTOCORE_LOGICAL_ADDRESS):
        """发送 UDS 诊断请求，返回响应字节"""
        if not self.routing_active:
            # 自动激活路由
            if not self.routing_activate():
                return None

        payload = struct.pack("!HH", source_addr, target_addr) + bytes(uds_data)
        self._send_doip(DOIP_DIAGNOSTIC_MESSAGE, payload)
        ptype, payload = self._recv_doip()

        if ptype == DOIP_DIAGNOSTIC_MESSAGE_ACK:
            # 解析诊断响应
            src = struct.unpack_from("!H", payload, 0)[0]
            tgt = struct.unpack_from("!H", payload, 2)[0]
            ack_code = payload[4]
            if ack_code == 0:
                uds_resp = payload[6:]
                return uds_resp
            else:
                print(f"[DoIP] NACK: code={ack_code}")
                return None
        elif ptype == DOIP_GENERIC_DOIP_NACK:
            print(f"[DoIP] NACK: {payload[0]}")
            return None
        return None

    def uds_request(self, sid, data=None):
        """发送 UDS 请求并打印响应"""
        req = [sid]
        if data:
            if isinstance(data, (bytes, bytearray)):
                req.extend(data)
            else:
                req.extend(data)

        print(f"\n>>> {sid_name(sid)}")
        print(f"    Request:  {' '.join(f'{b:02X}' for b in req)}")

        resp = self.send_uds(req)
        if resp is None:
            print("    No response")
            return None

        print(f"    Response: {' '.join(f'{b:02X}' for b in resp)}")

        if resp[0] == UDS_SID_NEGATIVE_RESPONSE:
            nrc = resp[2]
            nrc_name = NRC_NAMES.get(nrc, "Unknown")
            print(f"    ❌ NRC 0x{nrc:02X}: {nrc_name}")
        else:
            print(f"    ✅ OK ({len(resp)} bytes)")
            parse_uds_response(resp)

        return resp


# ==================== UDS 辅助函数 ====================

def sid_name(sid):
    """返回 SID 名称"""
    names = {
        0x10: "DiagnosticSessionControl",
        0x22: "ReadDataByIdentifier",
        0x2E: "WriteDataByIdentifier",
        0x27: "SecurityAccess",
        0x19: "ReadDTCInformation",
        0x14: "ClearDiagnosticInformation",
    }
    return names.get(sid, f"Unknown(0x{sid:02X})")


def did_name(did):
    """返回 DID 名称"""
    names = {
        0xF100: "EngineSpeed",
        0xF101: "VehicleSpeed",
        0xF102: "EngineTemperature",
        0xF190: "VIN (Vehicle Identification Number)",
        0xF195: "SoftwareVersion",
    }
    return names.get(did, f"Unknown(0x{did:04X})")


def parse_uds_response(resp):
    """解析并格式化 UDS 响应"""
    if len(resp) < 1:
        return
    sid = resp[0]

    # 肯定响应 SID = 请求 SID + 0x40
    req_sid = sid - 0x40 if sid >= 0x40 else sid

    if req_sid == 0x22:  # ReadDataByIdentifier
        if len(resp) >= 3:
            did = (resp[1] << 8) | resp[2]
            data = resp[3:]
            print(f"    DID: 0x{did:04X} ({did_name(did)})")
            if did == 0xF190:  # VIN
                print(f"    VIN: {data.decode('ascii', errors='replace')}")
            elif did in (0xF100, 0xF101, 0xF102) and len(data) >= 4:
                val = struct.unpack("<f", data[:4])[0]
                unit = {0xF100: "rpm", 0xF101: "km/h", 0xF102: "°C"}.get(did, "")
                print(f"    Value: {val:.2f} {unit}")
            elif did == 0xF195:
                print(f"    Version: {data.decode('ascii', errors='replace')}")
            else:
                print(f"    Data: {' '.join(f'{b:02X}' for b in data)}")

    elif req_sid == 0x2E:  # WriteDataByIdentifier
        if len(resp) >= 3:
            did = (resp[1] << 8) | resp[2]
            print(f"    DID: 0x{did:04X} ({did_name(did)}) - Written OK")

    elif req_sid == 0x10:  # DiagnosticSessionControl
        if len(resp) >= 2:
            session = {1: "Default", 2: "Programming", 3: "Extended"}.get(resp[1], "Unknown")
            print(f"    Session: {session}")

    elif req_sid == 0x27:  # SecurityAccess
        if len(resp) >= 2:
            subfunc = resp[1]
            if subfunc == 0x01:
                seed = int.from_bytes(resp[2:6], 'big') if len(resp) >= 6 else 0
                print(f"    Seed: 0x{seed:08X}")
            elif subfunc == 0x02:
                print(f"    ✅ Security Unlocked!")

    elif req_sid == 0x19:  # ReadDTCInformation
        if len(resp) >= 5 and resp[1] == 0x01:
            print(f"    DTC count: {resp[3]}")
        elif len(resp) >= 3 and resp[1] == 0x02:
            dtc_count = (len(resp) - 2) // 4
            print(f"    DTC entries: {dtc_count}")
            for i in range(dtc_count):
                offset = 2 + i * 4
                dtc = (resp[offset] << 16) | (resp[offset+1] << 8) | resp[offset+2]
                status = resp[offset+3]
                print(f"      DTC 0x{dtc:06X} status=0x{status:02X}")

    elif req_sid == 0x14:  # ClearDiagnosticInformation
        print(f"    ✅ All DTCs cleared")


# ==================== 交互模式 ====================

def interactive_mode(client):
    """交互式诊断控制台"""
    # 先激活路由
    client.routing_activate()

    print("\n===== AutoCore DoIP Diagnostic Console =====")
    print("Commands:")
    print("  session        - 读默认会话")
    print("  session ext    - 切换扩展会话")
    print("  read <DID>     - 读 DID (如 read F100)")
    print("  write <DID>    - 写 DID")
    print("  seed           - 请求种子")
    print("  key <value>    - 发送密钥")
    print("  dtc count      - 读 DTC 数量")
    print("  dtc list       - 读 DTC 列表")
    print("  dtc clear      - 清除 DTC")
    print("  help           - 帮助")
    print("  quit           - 退出")
    print("")

    while True:
        try:
            cmd = input("diag> ").strip().split()
            if not cmd:
                continue
            if cmd[0] == "quit":
                break
            elif cmd[0] == "session":
                if len(cmd) > 1:
                    sub = {"default": 0x01, "ext": 0x03, "extended": 0x03,
                           "prog": 0x02, "programming": 0x02}.get(cmd[1], None)
                    if sub:
                        client.uds_request(0x10, [sub])
                    else:
                        print(f"Unknown session: {cmd[1]}")
                else:
                    client.uds_request(0x10, [0x01])
            elif cmd[0] == "read":
                if len(cmd) > 1:
                    did = int(cmd[1], 16)
                    client.uds_request(0x22, [(did >> 8) & 0xFF, did & 0xFF])
                else:
                    print("Usage: read <DID>")
            elif cmd[0] == "write":
                if len(cmd) > 2:
                    did = int(cmd[1], 16)
                    data = [int(x, 16) for x in cmd[2:]]
                    client.uds_request(0x2E, [(did >> 8) & 0xFF, did & 0xFF] + data)
                else:
                    print("Usage: write <DID> <data bytes>")
            elif cmd[0] == "seed":
                client.uds_request(0x27, [0x01])
            elif cmd[0] == "key":
                if len(cmd) > 1:
                    key = int(cmd[1], 16)
                    key_bytes = [(key >> 24) & 0xFF, (key >> 16) & 0xFF,
                                 (key >> 8) & 0xFF, key & 0xFF]
                    client.uds_request(0x27, [0x02] + key_bytes)
                else:
                    print("Usage: key <hex_value>")
            elif cmd[0] == "dtc":
                if len(cmd) > 1:
                    if cmd[1] == "count":
                        client.uds_request(0x19, [0x01])
                    elif cmd[1] == "list":
                        client.uds_request(0x19, [0x02, 0xFF])
                    elif cmd[1] == "clear":
                        client.uds_request(0x14, [0xFF, 0xFF, 0xFF])
                    else:
                        print("dtc count|list|clear")
            elif cmd[0] == "help":
                print("Commands: session, read, write, seed, key, dtc, quit")
            else:
                print(f"Unknown: {cmd[0]}")
        except KeyboardInterrupt:
            break
        except Exception as e:
            print(f"Error: {e}")


# ==================== 一键测试 ====================

def run_all_tests(client):
    """运行全套诊断测试"""
    print("\n====== AutoCore DoIP Diagnostic Test Suite ======")
    print(f"Target: {client.host}:{client.port}\n")

    # 1. 路由激活
    print("--- 1. Routing Activation ---")
    if not client.routing_activate():
        print("❌ Routing activation failed")
        return
    print()

    # 2. 读 VIN
    print("--- 2. Read VIN (DID 0xF190) ---")
    client.uds_request(0x22, [0xF1, 0x90])
    print()

    # 3. 读软件版本
    print("--- 3. Read Software Version (DID 0xF195) ---")
    client.uds_request(0x22, [0xF1, 0x95])
    print()

    # 4. 读发动机转速
    print("--- 4. Read Engine Speed (DID 0xF100) ---")
    client.uds_request(0x22, [0xF1, 0x00])
    print()

    # 5. 会话控制（默认）
    print("--- 5. Diagnostic Session Control (Default) ---")
    client.uds_request(0x10, [0x01])
    print()

    # 6. 安全访问
    print("--- 6. Security Access - Request Seed ---")
    resp = client.uds_request(0x27, [0x01])
    if resp and len(resp) >= 6:
        # 解析种子并计算密钥
        seed = int.from_bytes(resp[2:6], 'big')
        key = seed ^ 0x12345678
        print(f"\n--- 7. Security Access - Send Key ---")
        print(f"    seed=0x{seed:08X}, key=0x{key:08X}")
        client.uds_request(0x27, [0x02,
                                   (key >> 24) & 0xFF,
                                   (key >> 16) & 0xFF,
                                   (key >> 8) & 0xFF,
                                   key & 0xFF])
    print()

    # 8. 读 DTC 数量
    print("--- 8. Read DTC Number ---")
    client.uds_request(0x19, [0x01])
    print()

    # 9. 切换扩展会话
    print("--- 9. Diagnostic Session Control (Extended) ---")
    client.uds_request(0x10, [0x03])
    print()

    print("====== All Tests Complete ======\n")


# ==================== 主入口 ====================

def main():
    parser = argparse.ArgumentParser(description="AutoCore DoIP Diagnostic Tool")
    parser.add_argument("host", nargs="?", default="192.168.1.100",
                        help="Target IP address (default: 192.168.1.100)")
    parser.add_argument("-p", "--port", type=int, default=13400,
                        help="DoIP port (default: 13400)")
    parser.add_argument("-i", "--interactive", action="store_true",
                        help="Interactive mode")
    parser.add_argument("-t", "--timeout", type=float, default=5.0,
                        help="Timeout in seconds (default: 5)")
    args = parser.parse_args()

    client = DoIPClient(host=args.host, port=args.port, timeout=args.timeout)

    try:
        client.connect()

        if args.interactive:
            interactive_mode(client)
        else:
            run_all_tests(client)

    except ConnectionRefusedError:
        print(f"❌ Connection refused: {args.host}:{args.port}")
        print("   Make sure the gateway is running on the target device")
        sys.exit(1)
    except socket.timeout:
        print(f"❌ Connection timed out: {args.host}:{args.port}")
        print("   Check: Is the device reachable? Is port 13400 open?")
        sys.exit(1)
    except Exception as e:
        print(f"❌ Error: {e}")
        sys.exit(1)
    finally:
        client.close()


if __name__ == "__main__":
    main()
