# 海康 MV-CS050-10GC 驱动接入与台架配置

**基准**:2026-11-26,开发机(Ubuntu 24.04.5, ROS 2 Jazzy)直连相机实测。车上部署的差异点在文末待办。

## 1. 硬件与网络

相机 MV-CS050-10GC(5MP GigE 彩色,IMX264,序列号 **K90022518**,固件 V3.4.54),直连开发机 `enp129s0`(Realtek RTL8168H,r8169 驱动)。传感器 LAN 网段沿用 `192.168.123.0/24`(`livox_mid360s.example.yaml` 的约定)。

| 项 | 值 |
|---|---|
| 相机静态 IP | `192.168.123.64/24`,网关 `192.168.123.222`(断电持久) |
| 相机用户名 | `hik_front` |
| 相机帧包大小 | 9000(volatile,driver 每次打开时下发电机参数) |
| 开发机网卡 | NetworkManager 配置 **`hik-cam`**:静态 `192.168.123.222/24`,MTU 9000 |

网卡复现命令:

```bash
nmcli con add type ethernet ifname enp129s0 con-name hik-cam \
  ipv4.method manual ipv4.addresses 192.168.123.222/24 ipv6.method disabled \
  802-3-ethernet.mtu 9000
nmcli con up hik-cam
```

r8169 在 MTU 9000 下满帧率零丢包;MVS 的 `GevSCPSPacketSize` 最优值 8164–9000 均验证过。

## 2. MVS SDK 持久化配置(改 IP 的正确姿势)

SDK 5.1.1(相机 SDK 4.9.0.3)装在 `/opt/MVS`,Python 包装器在 `/opt/MVS/Samples/64/Python/MvImport`(通过 `MVCAM_COMMON_RUNENV=/opt/MVS/lib` 加载 so)。

**坑:`MV_GIGE_ForceIpEx` / `MV_GIGE_SetIpConfig` 是引导层操作,必须在 `MV_CC_CreateHandle` 之后、`MV_CC_OpenDevice` 之前调用**;在已打开的句柄上调用报 `MV_E_CALLORDER`(0x80000003)/`MV_E_ACCESS_DENIED`(0x80000203)。改 IP 时相机随即失联属正常,重枚举即可。

设置持久静态 IP 的顺序:CreateHandle → `MV_GIGE_ForceIpEx(ip, mask, gw)` → `MV_GIGE_SetIpConfig(MV_IP_CFG_STATIC)` → DestroyHandle。

验证:断电重上电后枚举,IP 仍为 `192.168.123.64` 即持久化成功(枚举里的 `ip config` 位字段在该固件恒显 none,不可作为判据)。

## 3. ROS 驱动接入

驱动用 `camera_aravis2`(Aravis 0.8.30 GenICam,`ros-jazzy-camera-aravis2` 1.2.0,与车上约定一致);`ros-jazzy-camera-ros` 是 V4L2/libcamera 节点,**不能**驱动 GigE Vision 相机。

Aravis 设备 ID(即资产 yaml 的 `guid`):**`Hikrobot-MV-CS050-10GC-K90022518`**,可用 `ros2 run camera_aravis2 camera_finder` 列出。

台架参数文件(continuous 模式;车上触发参数见 `hikvision_mvcs050.example.yaml`):

```yaml
driver:
  ros__parameters:
    guid: Hikrobot-MV-CS050-10GC-K90022518
    frame_id: vehicle_001_front_camera_optical_frame
    stream_names: [vis]
    # 必须指向真实存在的文件,否则 driver 在 setupCameraStreamStructs 断言 FATAL
    camera_info_urls: [file:///tmp/mvs_config/mvcs050_camera_info.yaml]
    TransportLayerControl.PtpEnable: false   # 本固件无此特性,仅 WARN 不挡启动
    TransportLayerControl.GevSCPSPacketSize: 9000
    ImageFormatControl.Width: 2448
    ImageFormatControl.Height: 2048
    ImageFormatControl.PixelFormat: [BayerRG8]
    AcquisitionControl.TriggerMode: 'Off'    # 车上改 On + Line0 + RisingEdge
    AcquisitionControl.ExposureAuto: 'Off'
    AcquisitionControl.ExposureTime: 5000.0  # 台架推荐值见 §6
```

```bash
source /opt/ros/jazzy/setup.bash
export ROS_DOMAIN_ID=99   # 台架隔离用
export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
export CYCLONEDDS_URI=file:///tmp/mvs_config/cyclonedds_lo.xml  # 锁 loopback,原因见 §5
ros2 run camera_aravis2 camera_driver_gv --ros-args -r __node:=driver \
  --params-file /tmp/mvs_config/driver_test.yaml
```

话题:`/driver/vis/image_raw`(bayer_rggb8)+ `/driver/vis/camera_info`;节点名 `driver` 与资产 yaml 的 launcher 行为一致。无订阅者时 driver 自动停采集。占位 camera_info(未标定)在 `/tmp/mvs_config/mvcs050_camera_info.yaml`,真标定做出来后放 `/opt/chaos/hik/mvcs050_camera_info.yaml`。

## 4. 验证方法

- 相机直连层:`arv-tool-0.8 -n <guid> features | grep -i <feature>` 查 GenICam 特性;MVS 抓帧 60 张,23.16fps、`nLostPacket=0`。
- ROS 层:**stamp 间距诊断法**——订阅端同时记录帧到达率和相邻 `header.stamp` 中位差。生产率(23.14Hz)正常而到达率掉到 4.8Hz ⇒ 丢在 DDS 传输层,不在相机/驱动。
- driver 日志中 `Acquisition start/stop` 由订阅者数量驱动;偶发一次 `ARV_BUFFER_STATUS_TIMEOUT` 出现在启停切换瞬间,可忽略;持续报则是流异常。

## 5. DDS 大帧吞吐(5MB 帧的关键结论)

默认 RMW **FastDDS 内置流控对 >4MB 的消息限速** ~24MB/s:5MB 的 BayerRG8 帧发布端 23Hz 正常,订阅端只有 ~4.8Hz(43ms 帧距 ×4.8 ≈ 208ms 周期)。换 **CycloneDDS 后 loopback 满帧率 23.00Hz 到订阅端(139 帧基本零丢)**。调大 `net.core.rmem_default`(已调到 10MB)不能解决 FastDDS 限速。

**车上 Jetson 流 5MP 原图会撞同样的限**,bringup 建议:

1. 全栈统一 `RMW_IMPLEMENTATION=rmw_cyclonedds_cpp`(或把图降到 <4MB,如 BayerRG8 ROI ≤ 2448×1600);
2. 有 Mihomo/TUN 等虚拟网卡的机器上,CycloneDDS 会按默认路由选 TUN 网卡,跨 RMW 发现直接失败——用 `CYCLONEDDS_URI` 锁定物理网卡(loopback 台架用 `lo`):
   ```xml
   <CycloneDDS><Domain><General><Interfaces><NetworkInterface name="lo"/></Interfaces></General></Domain></CycloneDDS>
   ```
3. 不要同时跑两个 driver 实例:GigE 独占,后者重试 60 次后 FATAL。

## 6. 曝光标定(台架暗光办公室场景)

增益 0dB 的亮度-曝光线性段(0-255 制,p99 在亮屏处削顶):

| 曝光 | 5ms | 10ms | 20ms | 40ms | 80ms |
|---|---|---|---|---|---|
| 亮度均值 | 20.0 | 33.5 | 57.6 | 103.2 | 157.3 |

增益实测有效且线性(20ms 固定):3dB→76.6,6dB→101.5,9dB→132.0,12dB→158.3。相机 `ExposureAuto=Continuous` 自收敛 42.7ms(均值 ~105)。

**台架推荐:曝光 20ms + 增益 9dB(均值 ≈132),保持 23.14fps 满帧率**;更干净的画质用 20ms + 6dB(≈101)。40ms + 0dB(≈103)也可但运动模糊大。运动场景(车上 LIVO)曝光建议 ≤10ms,车上光照另测。

注意:自动曝光开启时 `ExposureTime` 不可写(报 0x80000106);先 `ExposureAuto=Off` 再设固定值。相机内该参数 volatile,车上由 driver 每次下发。

## 7. 待办

- [ ] 相机断电重上电一次,枚举确认 IP 持久化(§2)。
- [ ] 镜头对焦(当前 20ms+9dB 图像失焦,手动拧调焦环;对焦后重新标定)。
- [ ] 棋盘格标定 → `/opt/chaos/hik/mvcs050_camera_info.yaml`,替换台架占位文件。
- [ ] 车上:触发模式(板卡 CAM_1 PWM 10Hz→Line0)、按车端光照定曝光、外参测量。
