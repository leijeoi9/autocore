#!/usr/bin/env python3
"""
ISO-TP 多帧发送测试工具

测试网关的 ISO-TP 多帧响应。
发一个需要多帧响应的 UDS 请求（如读 VIN 17 字节），
看网关是否完整回复了所有 CF。
"""
import socket
import struct
import time

HOST = "127.0.0.1"
PORT = 13400

# DoIP 常量
VER = 0x02
INV = 0xFD
ROUTING_ACTIVATION_REQ = 0x0007
ROUTING_ACTIVATION_RESP = 0x0008
DIAGNOSTIC_MSG = 0x8001
DIAGNOSTIC_MSG_ACK = 0x8002

# 逻辑地址
ECU_ADDR = 0x0E80
TESTER_ADDR = 0x0000

def send_doip(sock, ptype, payload):
    header = struct.pack("!BBHI", VER, INV, ptype, len(payload))
    sock.sendall(header + payload)

def recv_doip(sock):
    header = sock.recv(8)
    if len(header) < 8:
        return None, None
    _, _, ptype, plen = struct.unpack("!BBHI", header)
    payload = b""
    while len(payload) < plen:
        chunk = sock.recv(plen - len(payload))
        if not chunk:
            break
        payload += chunk
    return ptype, payload

print("=" * 60)
print("ISO-TP Multi-Frame Test")
print("=" * 60)

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.settimeout(5)
sock.connect((HOST, PORT))
print(f"Connected to {HOST}:{PORT}")

# 1. 路由激活
payload = struct.pack("!HBBBBBB", 0, 1, 0, 0, 0, 0, 0)
send_doip(sock, ROUTING_ACTIVATION_REQ, payload)
ptype, payload = recv_doip(sock)
print(f"Routing: OK" if ptype == ROUTING_ACTIVATION_RESP else f"Routing: FAIL ({ptype})")

# 2. 读 VIN（17 字节，需要多帧）
print("\n--- Test 1: Read VIN (17 bytes) ---")
payload = struct.pack("!HH", 0, ECU_ADDR) + bytes([0x22, 0xF1, 0x90])
send_doip(sock, DIAGNOSTIC_MSG, payload)
start = time.time()
ptype, payload = recv_doip(sock)
elapsed = time.time() - start

if ptype == DIAGNOSTIC_MSG_ACK:
    uds_resp = payload[6:]
    print(f"Response ({len(uds_resp)} bytes) in {elapsed*1000:.1f}ms")
    print(f"Hex: {uds_resp.hex()}")
    if len(uds_resp) > 3:
        vin = uds_resp[3:].decode('ascii', errors='replace')
        print(f"VIN: {vin}")
    if len(uds_resp) > 7:
        print(f"✅ Multi-frame TX works! (response > 7 bytes = {len(uds_resp)} bytes)")
    else:
        print(f"⚠️  Response fits in single frame (<=7 bytes)")
else:
    print(f"FAIL: ptype=0x{ptype:04x}")

# 3. 读发动机转速 + 车速 + 温度（12 字节，也需要多帧）
print("\n--- Test 2: Read multiple DIDs ---")
payload = struct.pack("!HH", 0, ECU_ADDR) + bytes([0x22, 0xF1, 0x00])
send_doip(sock, DIAGNOSTIC_MSG, payload)
ptype, payload = recv_doip(sock)

if ptype == DIAGNOSTIC_MSG_ACK:
    uds_resp = payload[6:]
    print(f"Response ({len(uds_resp)} bytes)")
    print(f"Hex: {uds_resp.hex()}")
else:
    print(f"FAIL: ptype=0x{ptype:04x}")

# 4. 安全访问 + 解锁（验证序列化请求）
print("\n--- Test 3: Security Access (seed + key) ---")

# 请求种子
payload = struct.pack("!HH", 0, ECU_ADDR) + bytes([0x27, 0x01])
send_doip(sock, DIAGNOSTIC_MSG, payload)
ptype, payload = recv_doip(sock)

if ptype == DIAGNOSTIC_MSG_ACK:
    uds_resp = payload[6:]
    print(f"Seed response ({len(uds_resp)} bytes): {uds_resp.hex()}")
    if len(uds_resp) >= 6:
        seed = int.from_bytes(uds_resp[2:6], 'big')
        key = seed ^ 0x12345678
        print(f"seed=0x{seed:08X}, key=0x{key:08X}")

        # 发送密钥
        key_bytes = [(key >> 24) & 0xFF, (key >> 16) & 0xFF,
                     (key >> 8) & 0xFF, key & 0xFF]
        payload = struct.pack("!HH", 0, ECU_ADDR) + bytes([0x27, 0x02] + key_bytes)
        send_doip(sock, DIAGNOSTIC_MSG, payload)
        ptype, payload = recv_doip(sock)
        if ptype == DIAGNOSTIC_MSG_ACK:
            uds_resp = payload[6:]
            if uds_resp[0] == 0x7F:
                print(f"❌ Security: NRC=0x{uds_resp[2]:02X}")
            else:
                print(f"✅ Security: Unlocked! {uds_resp.hex()}")
else:
    print(f"FAIL: ptype=0x{ptype:04x}")

# 5. 读故障码
print("\n--- Test 4: Read DTC ---")
payload = struct.pack("!HH", 0, ECU_ADDR) + bytes([0x19, 0x01])
send_doip(sock, DIAGNOSTIC_MSG, payload)
ptype, payload = recv_doip(sock)
if ptype == DIAGNOSTIC_MSG_ACK:
    uds_resp = payload[6:]
    print(f"DTC response: {uds_resp.hex()}")

sock.close()
print("\n" + "=" * 60)
print("All tests complete!")
print("=" * 60)
