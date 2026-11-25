# 奥比中光 Gemini 335 驱动接入

Gemini 335 使用官方 C++ ROS 2 驱动；资产配置负责设备选择、ROS namespace、参数和标准数据接口，统一 launch 管理进程。原厂源码位于 [`src/chaos_sensors/vendor/orbbec/src/OrbbecSDK_ROS2`](../src/chaos_sensors/vendor/orbbec/src/OrbbecSDK_ROS2)，不修改厂商的 USB/SDK 实现。

## 1. 来源与版本

| 项目 | 固定值 |
| --- | --- |
| 官方仓库 | [orbbec/OrbbecSDK_ROS2](https://github.com/orbbec/OrbbecSDK_ROS2) |
| 正式版本 | [v2.10.6](https://github.com/orbbec/OrbbecSDK_ROS2/releases/tag/v2.10.6)，2026-09-30 发布 |
| Commit | `296156521b60bdf813cb94c46cbb55562bbdc453` |
| ROS 包 | `orbbec_camera`、`orbbec_camera_msgs`；仓库同时保留 `orbbec_description` |
| SDK | 原厂随版本提供的 OrbbecSDK 2.10.6，包含 Linux x64 和 ARM64 二进制 |
| 源码恢复清单 | [`src/chaos_sensors/vendor/orbbec/orbbec.repos`](../src/chaos_sensors/vendor/orbbec/orbbec.repos) |

此版本声明支持 ROS 2 Jazzy，并提供 Ubuntu 24.04 x64 和 ARM64 平台支持信息。ARM64 的厂商测试平台主要为 Jetson；其他 ARM64 板卡仍需核对其系统与 USB 支持。源码 CMake 明确拒绝 ARM32。以上依据固定版本的 [README](../src/chaos_sensors/vendor/orbbec/src/OrbbecSDK_ROS2/README.MD) 与 [CMakeLists.txt](../src/chaos_sensors/vendor/orbbec/src/OrbbecSDK_ROS2/orbbec_camera/CMakeLists.txt)。

完整保留上游 Apache-2.0 [`LICENSE`](../src/chaos_sensors/vendor/orbbec/src/OrbbecSDK_ROS2/LICENSE)、[`NOTICE`](../src/chaos_sensors/vendor/orbbec/src/OrbbecSDK_ROS2/NOTICE) 和 [`SDK/licenses`](../src/chaos_sensors/vendor/orbbec/src/OrbbecSDK_ROS2/orbbec_camera/SDK/licenses/THIRD_PARTY_LICENSES.md)。分发原厂 SDK 时应一并保留这些材料。

当前用户设备为 Gemini 335。v2.10.6 的型号表将其对应到 `gemini_330_series.launch.py`，推荐固件为 1.8.10；本轮没有执行固件升级。旧 OpenNI 设备需要另行核对官方 `main` 分支，不能直接套用此 v2 目录。

## 2. 独立构建与环境

框架与原厂驱动分开构建。在 `chaos_ws` 下执行：

```bash
./src/chaos_sensors/vendor/orbbec/build-orbbec.sh
source /opt/ros/jazzy/setup.bash
source src/chaos_sensors/vendor/orbbec/install/local_setup.bash
source install/local_setup.bash
```

脚本仅构建 `orbbec_camera` 及其消息依赖，使用本目录 `src/chaos_sensors/vendor/orbbec/build/install/log`，不安装系统依赖。脚本清除 `BUILDING_PACKAGE` 并设置 `INSTALL_UDEV_RULES=OFF`，因此普通源码构建不会写入 `/etc/udev/rules.d`。

目标机依赖以固定源码的 [`package.xml`](../src/chaos_sensors/vendor/orbbec/src/OrbbecSDK_ROS2/orbbec_camera/package.xml)、`CMakeLists.txt` 和 README 为准。当前开发机实际缺失：

- ROS 包：`backward_ros`、`image_publisher`、`camera_info_manager`、`diagnostic_updater`、`camera_calibration_parsers`。
- 系统包：`libgoogle-glog-dev`、`python3-tabulate`。

对应 Ubuntu/ROS 包名为 `ros-jazzy-backward-ros`、`ros-jazzy-image-publisher`、`ros-jazzy-camera-info-manager`、`ros-jazzy-diagnostic-updater`、`ros-jazzy-camera-calibration-parsers` 及上述系统包名；部署环境完成这些依赖后再运行构建脚本。本轮没有安装这些包，也未修改 udev 规则。

生产环境在目标 CPU 架构上使用普通安装，例如从 `chaos_ws/drivers` 执行：

```bash
source /opt/ros/jazzy/setup.bash
unset BUILDING_PACKAGE
# /opt/chaos/drivers 需预先由部署流程准备为可写目录。
colcon --log-base log-onboard build --base-paths src \
  --build-base build-onboard --merge-install --install-base /opt/chaos/drivers \
  --packages-up-to orbbec_camera --executor sequential \
  --cmake-args -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DINSTALL_UDEV_RULES=OFF
```

在机载 `edge.env` 中配置 `CHAOS_DRIVER_SETUP=/opt/chaos/drivers/local_setup.bash`，启动入口会在框架前加载驱动环境。USB 权限按固定版本原厂规则交由设备部署流程配置；此处的构建不会自动获得相机访问权限。不要直接复制 x64 的编译产物到 ARM64。

## 3. 资产配置与统一启动

完整模板为 [`orbbec_gemini335.example.yaml`](../src/chaos_assets/config/orbbec_gemini335.example.yaml)，包含单台 `vehicle_001` 和传感器 `front_camera`。复制模板后填写真实 `serial_number`；多相机部署为每台设备分配不同序列号和 `camera_name`。

框架直接启动 `orbbec_camera/orbbec_camera_node`。上游通过 `rclcpp_components_register_node` 同时生成这个独立可执行文件和 `orbbec_camera::OBCameraNodeDriver` 组件。官方 Jazzy 型号 launch 常用组件容器，本框架采用独立进程，便于统一退出管理。

模板的 ROS namespace 为 `/robots/vehicle_001/sensors/front_camera`；`camera_name=vehicle_001_front_camera` 决定原厂内部 TF 前缀。二者应分别配置，不能把 ROS namespace 当作 TF 前缀。

在 `chaos_ws` 下先查看计划：

```bash
source /opt/ros/jazzy/setup.bash
source install/local_setup.bash
ros2 launch chaos_bringup edge.launch.py \
  inventory:="$PWD/src/chaos_assets/config/orbbec_gemini335.example.yaml" \
  asset_id:=vehicle_001 enable_mqtt:=false dry_run:=true
```

真正启动前完成原厂驱动构建，加载其 `local_setup.bash`，填写相机序列号与实际底盘 topic，再去掉 `dry_run:=true`。模板的车辆适配器仍连接现有底盘驱动；本轮仅把相机纳入统一 launch。

`base_link → vehicle_001_front_camera_link` 的安装外参需实测后写入配置，模板没有默认把相机放在车体原点。`publish_tf=true` 让原厂驱动发布相机内部标定得到的 TF，框架无需重复发布这些内部变换。

## 4. 标准 ROS 数据与默认负载

下表中的 topic 都相对于传感器 namespace；`prefix` 表示 `vehicle_001_front_camera`。

| Topic | 消息 | 默认 header.frame_id / 用途 |
| --- | --- | --- |
| `color/image_raw` | `sensor_msgs/msg/Image` | `prefix_color_optical_frame` |
| `color/camera_info` | `sensor_msgs/msg/CameraInfo` | `prefix_color_optical_frame` |
| `depth/image_raw` | `sensor_msgs/msg/Image` | `prefix_depth_optical_frame`，未开启深度配准 |
| `depth/camera_info` | `sensor_msgs/msg/CameraInfo` | 与深度图一致 |
| `gyro_accel/sample` | `sensor_msgs/msg/Imu` | `prefix_accel_gyro_optical_frame` |
| `depth/points` | `sensor_msgs/msg/PointCloud2` | 由 `enable_point_cloud` 开启 |
| `depth_registered/points` | `sensor_msgs/msg/PointCloud2` | 由 `enable_colored_point_cloud` 开启 |

模板启用 RGB、深度与内置 IMU，默认关闭普通和彩色点云。普通点云和深度到彩色的配准是独立选项，按算法需求开启。设置 `depth_registration=true` 后，深度消息会使用彩色光学坐标系，应同步修改资产中的流 frame 声明。启用新的点云输出时也应增加对应的流声明。

图像分辨率和帧率为 `0` 表示交给 SDK 选择设备支持的 profile；这不是确定的性能预算。上车时按 USB 带宽、CPU 与算法输入要求明确设置分辨率、帧率，再用实机验证。模板启用 frame sync，并使用 `time_domain=global`；其他机器人通过同类配置使用自己的 namespace 和 TF 前缀。

Gemini 335 内置加速度计和陀螺仪，官方产品规格确认支持 IMU。[Gemini 335 官方规格](https://www.orbbec.com/products/stereo-vision-camera/gemini-335/)

`enable_sync_output_accel_gyro=true` 产生同步的加速度与角速度。原厂驱动把 `orientation_covariance[0]` 设为 `-1`，表示没有姿态估计；姿态融合由后续算法提供。图像流的监测默认关闭，CameraInfo 和 IMU 的监测用于观察输入是否持续到达，避免 Python 监测节点为统计而复制高带宽图像。

## 5. 本轮验证与真机验收

2026-10-16 已完成：下载正式 tag，核对精确 commit，检查上游工作树未修改；确认仓库内 x64 和 ARM64 SDK 为对应架构的 ELF 库；脚本通过 `bash -n`；在独立驱动目录尝试源码构建。

构建结果为 `orbbec_camera_msgs` 成功，`orbbec_camera` 的 CMake 在缺少 `backward_ros` 时停止。因此源码集成已完成，但当前开发机尚未生成可运行的相机节点，也没有连接真实 Gemini 335 验证图像、IMU、USB 重连或时间戳。

实机验收应确认序列号选择正确、RGB/深度/IMU 持续输出、frame 与 TF 树一致、时间戳单调且与系统时钟可对应，再测试拔插恢复和统一关闭。需要跨机器人融合时，再验证设备时钟同步和相机到机体的标定外参。
