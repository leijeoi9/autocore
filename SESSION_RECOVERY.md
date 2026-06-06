# 2026-06-06 项目现场恢复文件
# 下次启动会话时：先读这个文件，再看 20260606task.md 和 README.md
# 然后 git log 看提交历史

## 一、会话恢复指令（下次直接说这句）
"继续 AutoCore 项目，先看 20260606task.md 和 git log 恢复上下文"

## 二、项目状态
- 所有代码已提交 git（master 分支，13 个 commit）
- 交叉编译通过（ARM Cortex-A7）
- 在 I.MX6ull 板子上成功运行（CAN 不可用模式，DoIP+UDS 正常）
- 备份在 /home/leijeoi/autocore_backup/

## 三、待办事项（优先级从高到低）
1. CAN 收发器到货后接线测试（需要买 SN65HVD230 模块）
2. 修改代码支持 can0/vcan0 切换（目前 rte.c 硬编码了 "vcan0"）
3. 扩展更多 UDS DID
4. 写 Python 诊断测试工具（通过 DoIP TCP 13400）
5. NVRAM 持久化

## 四、板子环境信息
- IP：串口查（ifconfig eth1），之前设的是 192.168.1.100（临时，重启丢失）
- 登录：root / 空密码
- SSH：未安装（尝试装 dropbear）
- vcan：内核不支持 vcan 模块
- gcc：板子上没有编译器
- python：板子上没有
- 文件传输：U盘 / base64 粘贴
- U盘挂载：mount /dev/sda1 /mnt（如果格式是 NTFS/exFAT 则板子不支持）
- 串口速率：115200

## 五、CAN 收发器接线指南（到货后用）
I.MX6ull CAN_TX → 收发器 D (TXD)
I.MX6ull CAN_RX → 收发器 R (RXD)
收发器 CANH → 另一设备 CANH
收发器 CANL → 另一设备 CANL
两端 120Ω 终端电阻

## 六、板端运行命令
```bash
modprobe can
modprobe can_raw
modprobe flexcan
ip link set can0 up type can bitrate 500000
cd /root/autocore && ./can_service
```

## 七、文件传输方式（下次传文件用）
Windows 上用 certutil 转 base64：
  certutil -encode file file.b64
板子上解码：
  sed '1d;$d' file.b64 | base64 -d > file

## 八、关键文件索引
- 项目总文档：/home/leijeoi/autocore/README.md
- 今日工作汇总：/home/leijeoi/autocore/20260606task.md
- 顶层 CMake：/home/leijeoi/autocore/CMakeLists.txt
- 入口 main.c：app/can_service/src/main.c
- RTE 接口：rte/include/rte.h
- RTE 实现：rte/src/rte.c
- ISO-TP：bsw/diag/iso_tp/
- UDS：bsw/diag/uds/
- DoIP：bsw/tcp_ip/doip/
- ARM 编译产物：build-arm/app/can_service/can_service
- 备份：/home/leijeoi/autocore_backup/
