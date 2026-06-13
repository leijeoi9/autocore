#!/usr/bin/env python3
"""
AutoCore CAN UDS 诊断测试工具

在板子上运行（需要 can0 接口），通过 CAN 总线发送 UDS 请求并接收响应。
自动处理 ISO-TP 协议（包括多帧 FC 回复）。
"""
import socket
import struct
import time
import fcntl
import os

# SocketCAN 常量
AF_CAN = 29  # Linux socket family for CAN
PF_CAN = AF_CAN
CAN_RAW = 1
SOL_CAN_RAW = 1
CAN_RAW_FILTER = 1

# CAN 帧结构
class CanFrame:
    def __init__(self, can_id=0, data=b''):
        self.can_id = can_id
        self.can_dlc = len(data)
        self.data = data.ljust(8, b'\x00')[:8]

def create_raw_socket(ifname):
    """创建 SocketCAN 原始 socket"""
    s = socket.socket(PF_CAN, socket.SOCK_RAW, CAN_RAW)
    # 绑定到接口
    ifr = struct.pack('16s', ifname.encode()[:16])
    fcntl.ioctl(s, 0x89E0, ifr)  # SIOCGIFINDEX
    return s

def can_send(s, frame):
    """发送 CAN 帧"""
    can_id = frame.can_id
    if can_id & 0x80000000:
        can_id &= 0x7FFFFFFF
        can_id |= 0x80000000  # 扩展帧
    else:
        can_id &= 0x7FF
    # SocketCAN 帧格式
    can_frame = struct.pack('=IB3x8s', can_id, frame.can_dlc, frame.data[:8])
    s.send(can_frame)

def can_recv(s, timeout=1.0):
    """接收 CAN 帧"""
    s.settimeout(timeout)
    try:
        data = s.recv(16)
        can_id, can_dlc = struct.unpack_from('=IB', data)
        can_id &= 0x1FFFFFFF
        frame_data = data[8:8+can_dlc]
        return CanFrame(can_id, frame_data)
    except socket.timeout:
        return None
    except BlockingIOError:
        return None

# ISO-TP 常量
PCI_SF  = 0x00  # 单帧
PCI_FF  = 0x10  # 首帧
PCI_CF  = 0x20  # 连续帧
PCI_FC  = 0x30  # 流控帧

def isotp_send(s, can_id, data):
    """发送 ISO-TP 报文"""
    length = len(data)
    if length <= 7:
        # 单帧
        pci = PCI_SF | length
        frame = CanFrame(can_id, bytes([pci]) + data[:7])
        can_send(s, frame)
        print(f"[TX] SF: id=0x{can_id:x}, len={length}")
    else:
        # 首帧
        pci = PCI_FF | ((length >> 8) & 0x0F)
        frame = CanFrame(can_id, bytes([pci, length & 0xFF]) + data[:6])
        can_send(s, frame)
        print(f"[TX] FF: id=0x{can_id:x}, total={length}")

        sent = 6
        seq = 1
        while sent < length:
            # 等待 FC 或等一小段时间后发 CF
            remaining = length - sent
            cf_len = min(remaining, 7)
            pci = PCI_CF | (seq & 0x0F)
            frame = CanFrame(can_id, bytes([pci]) + data[sent:sent+cf_len])
            can_send(s, frame)
            print(f"[TX] CF: seq={seq}, len={cf_len}, remaining={remaining-cf_len}")
            sent += cf_len
            seq = (seq + 1) & 0x0F
            time.sleep(0.01)  # 10ms 间隔

def isotp_recv(s, timeout=3.0):
    """接收 ISO-TP 报文（包含多帧）"""
    frame = can_recv(s, timeout)
    if frame is None:
        return None, None

    can_id = frame.can_id
    pci = frame.data[0] & 0xF0

    if pci == PCI_SF:
        length = frame.data[0] & 0x0F
        data = frame.data[1:1+length]
        print(f"[RX] SF: id=0x{can_id:x}, len={length}")
        return can_id, data

    elif pci == PCI_FF:
        length = ((frame.data[0] & 0x0F) << 8) | frame.data[1]
        data = bytearray(frame.data[2:8])
        received = 6
        # 回复 FC
        fc = CanFrame(can_id, bytes([PCI_FC | 0x00, 0x00, 0x00]))
        can_send(s, fc)
        print(f"[RX] FF: id=0x{can_id:x}, total={length}")

        # 接收 CF
        while received < length:
            cf = can_recv(s, timeout)
            if cf is None:
                print("[RX] CF timeout!")
                break
            seq = cf.data[0] & 0x0F
            cf_len = cf.can_dlc - 1
            data.extend(cf.data[1:1+cf_len])
            received += cf_len
            print(f"[RX] CF: seq={seq}, cf_len={cf_len}, total={received}/{length}")

        print(f"[RX] Complete: id=0x{can_id:x}, len={received}")
        return can_id, bytes(data[:received])

    elif pci == PCI_FC:
        print(f"[RX] FC: status={frame.data[0] & 0x0F}, bs={frame.data[1]}, st={frame.data[2]}")
        return can_id, b'FC'

    else:
        print(f"[RX] Unknown PCI: 0x{pci:02x}")
        return can_id, frame.data

def uds_request(s, req_id, resp_id, data):
    """发送 UDS 请求并接收响应"""
    print(f"\n>>> UDS Request: {' '.join(f'{b:02X}' for b in data)}")
    isotp_send(s, req_id, bytes(data))
    rx_id, rx_data = isotp_recv(s)
    if rx_data:
        print(f"    Response: {' '.join(f'{b:02X}' for b in rx_data)}")
        if rx_data[0] == 0x7F:
            print(f"    ❌ NRC=0x{rx_data[2]:02X}")
        else:
            print(f"    ✅ OK ({len(rx_data)} bytes)")
            # 解析常见响应
            sid = rx_data[0]
            if sid == 0x62:  # ReadDataByIdentifier 响应
                did = (rx_data[1] << 8) | rx_data[2]
                print(f"    DID=0x{did:04X}")
                if did == 0xF190:
                    vin = rx_data[3:].decode('ascii', errors='replace')
                    print(f"    VIN: {vin}")
    else:
        print("    ❌ Timeout")
    return rx_data

def main():
    print("=" * 60)
    print("AutoCore CAN UDS Diagnostic Tool (SocketCAN)")
    print("=" * 60)
    print("Opening can0...")

    try:
        import fcntl
        # 创建 raw socket
        s = socket.socket(PF_CAN, socket.SOCK_RAW, CAN_RAW)

        # 绑定到 can0
        ifr = struct.pack('16sH', b'can0', 0)
        try:
            fcntl.ioctl(s, 0x89E0, ifr)  # SIOCGIFINDEX
        except:
            pass
        # 简单方式：用 struct 设置 ifindex
        ifr = struct.pack('16si', b'can0\0', 0)
        import array
        import ctypes
        
        # 更可靠的方式
        SIOCGIFINDEX = 0x8933
        buf = array.array('B', b'can0\0' + b'\x00' * 12)
        ifr = bytes(buf)
        res = fcntl.ioctl(s, SIOCGIFINDEX, ifr)
        ifindex = struct.unpack_from('i', res, 16)[0]

        addr = struct.pack('=Hi', AF_CAN, ifindex)
        s.bind(addr)
        print("Connected to can0")

        req_id = 0x7E0  # 请求 ID
        resp_id = 0x7E8 # 响应 ID

        # 测试 1: 读 VIN
        print("\n--- Test 1: Read VIN ---")
        uds_request(s, req_id, resp_id, [0x22, 0xF1, 0x90])

        # 测试 2: 读发动机转速
        print("\n--- Test 2: Read Engine Speed ---")
        uds_request(s, req_id, resp_id, [0x22, 0xF1, 0x00])

        # 测试 3: 读软件版本
        print("\n--- Test 3: Read Software Version ---")
        uds_request(s, req_id, resp_id, [0x22, 0xF1, 0x95])

        # 测试 4: 读 DTC
        print("\n--- Test 4: Read DTC ---")
        uds_request(s, req_id, resp_id, [0x19, 0x01])

        s.close()
        print("\nAll tests complete!")

    except ImportError as e:
        print(f"Error: {e}")
        print("This tool requires Linux SocketCAN support (fcntl)")
    except Exception as e:
        print(f"Error: {e}")
        import traceback
        traceback.print_exc()

if __name__ == "__main__":
    main()
