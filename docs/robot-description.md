# URDF / Xacro 模型导入

通过资产的 `description` 配置导入整机 URDF 或 Xacro，统一 launch 为每台机器人启动独立的 `robot_state_publisher`。模型负责机体和关节结构；底盘、飞控、导航及传感器驱动仍负责各自的真实数据和控制。

目前没有用户设备的整机模型，因此提供 [示例型号](../src/chaos_assets/models/demo_ugv/v1/model.yaml) 和 [双机器人清单](../src/chaos_assets/config/description.demo.yaml)。示例中的尺寸、轮子和 IMU 安装位置都是合成数据，仅用于模型导入与 TF 验证，不能作为实机标定、碰撞模型或动力学参数。

## 配置入口

型号文件和资产实例均可声明 `description`，实例覆盖型号默认值。未声明时保持原有启动行为；`enabled: false` 可关闭该实例的模型发布。

```yaml
# 可放入 model.yaml，也可放在 assets 中的某个实例下。
description:
  file: package://my_robot_description/urdf/robot.urdf.xacro
  format: xacro
  arguments:
    wheel_radius: 0.08
  publish_frequency: 30.0
  joint_states_topic: joint_states
  use_sim_time: false
```

| 字段 | 规则 |
|---|---|
| `file` | 必填；绝对路径、相对路径或 `package://` 资源 |
| `format` | `urdf` / `xacro`；省略时由文件后缀推断 |
| `arguments` | Xacro 参数映射，支持字符串、布尔和有限数值；URDF 不接受参数 |
| `frame_prefix` | 默认 `<asset_id>/`；也可为空串或以 `/` 结尾的合法 frame 路径 |
| `publish_frequency` | 活动关节 TF 最大发布频率，默认 30 Hz，范围 0.1–1000 Hz |
| `joint_states_topic` | 默认该资产 namespace 下的 `joint_states`，也可指定绝对话题 |
| `use_sim_time` | 默认 false；开启后需要外部提供 `/clock` |
| `enabled` | 默认 true |

相对路径以声明它的文件为基准，型号中的路径不会改用实例文件目录。推荐将 URDF、Xacro、mesh 与纹理放进独立描述包，或本工程的 `chaos_assets/models/<model>/<version>/`；该目录内容会随 `chaos_assets` 安装。实例清单放入 `/etc/chaos` 后仍可通过 `package://` 引用安装资源。

同一文件的实例参数与型号参数合并；实例改用另一文件时不继承旧文件的 Xacro 参数，未显式指定 format 时也重新按后缀推断，避免换模板后沿用不兼容的参数。未展开的 Xacro 元素或属性会被拒绝，不能把它们当成普通 URDF 静默忽略。

Xacro 参数用字典传给独立解释进程，不拼接 shell 命令。Xacro 支持表达式、include、YAML 和环境变量读取，属于可信本地模型代码；只导入已检查来源的模型。展开和原生校验有超时与输出大小限制，不将 Xacro 当作不可信输入沙箱。Xacro 的原生能力见 [官方项目](https://github.com/ros/xacro/tree/2.1.1)。

## 构建与试运行

需要 ROS 2 Jazzy 的 `robot_state_publisher` 和 `urdfdom`（提供 `check_urdf`）；使用 Xacro 时另需 `xacro`。对应 Ubuntu 软件包为 `ros-jazzy-robot-state-publisher`、`ros-jazzy-urdfdom`、`ros-jazzy-xacro`。纯 URDF 路径不导入 Xacro。当前开发机的 Xacro 验证使用 `/tmp` 中的独立测试安装，没有替用户修改系统依赖。

在 `chaos_ws` 下执行：

```bash
source /opt/ros/jazzy/setup.bash
colcon build --base-paths src --symlink-install
source install/setup.bash

# 校验两个模型并显示启动计划；不启动 ROS 节点
ros2 launch chaos_bringup system.launch.py \
  inventory:="$PWD/src/chaos_assets/config/description.demo.yaml" dry_run:=true

# 启动两个 mock 机器人、各自的模型发布器和模拟 IMU
ros2 launch chaos_bringup system.launch.py \
  inventory:="$PWD/src/chaos_assets/config/description.demo.yaml"

# 安装 xacro 后，可检查参数化模型及相对 include
ros2 launch chaos_bringup system.launch.py \
  inventory:="$PWD/src/chaos_assets/config/description.xacro.example.yaml" dry_run:=true
```

`dry_run` 会实际读取模型、展开 Xacro、执行本地 `check_urdf` 并检查资源和 TF 归属；不会启动 RSP、硬件驱动或 ROS 数据连接，因此仍需要解析用的依赖。真实启动还会预检 RSP 可执行文件。模型在资产解析阶段展开为不可变 XML 快照，启动阶段不重新读取源模型；`sha256` 和资产 `config_digest` 包含本次模型结果。

原始文件保持不变。检查器会将相对 mesh/texture 文件名在冻结 XML 中转换为绝对 `file://` URI；`package://` 保留，并检查安装资源存在。拒绝远程 URL、带 host 的 file URI 和缺失文件。相对网格路径按入口模型目录解释，即使标签来自 Xacro include；跨目录资源优先使用 `package://`。

模型 XML 已冻结不代表 mesh/纹理被打包进消息。RViz 所在机器仍需能访问这些文件；跨设备显示应安装相同描述包并使用 `package://`，避免引用机载计算机私有的绝对路径。ROS 资源解析依据 [resource_retriever](https://github.com/ros/resource_retriever/blob/jazzy/resource_retriever/src/retriever.cpp#L126-L148)。

## 话题与坐标系

每台资产的发布器位于 `<asset namespace>/robot_state_publisher`：

- `robot_description`：模型 XML，可靠、transient local；晚加入的显示端仍可读取。
- `joint_states`：活动关节状态输入，名称仍使用原始 URDF 中的 joint 名。
- `/tf_static`：固定关节变换；不需要伪造 joint_states。
- `/tf`：由实际 joint_states 驱动的活动关节变换。

这些行为沿用 [Jazzy robot_state_publisher](https://github.com/ros/robot_state_publisher/tree/jazzy)。本框架不自动启动 `joint_state_publisher`，也不会把未知轮子或关节位置发布为零。示例运行后只会立即出现固定连接；动态轮子 TF 由测试脚本显式发布的模拟 JointState 验证。

`frame_prefix` 只影响 RSP 输出的 TF，不改写 XML 中的 link/joint 名。默认资产 `demo_001` 的 `base_link` 会对应 `demo_001/base_link`。同 domain 的 TF 话题是共享的，单独 ROS namespace 不足以隔离 frame。

导入后的根 frame 必须等于资产 `frames.base`。例如 URDF 根为 `base_footprint` 时，需要将资产的 base 配置为 `<asset_id>/base_footprint`，并核对底层 Odometry 的 child frame。模型本身不发布 `map → odom → base` 定位链，也不把机器人状态话题自动转换成定位 TF。

检查器在启动前拒绝重复 link/joint、无效 URDF 树、根 frame 不匹配、占用机器人 odom/map 或其他资产坐标系、重复 TF child，以及 URDF 与显式传感器变换组成的环。当前支持 fixed、revolute、continuous、prismatic 关节；不支持 floating、planar 或串联 mimic。直接 mimic 的目标必须是存在的活动关节，且不能循环。

有效 frame 采用本框架统一命名规则：各段以字母或下划线开头，仅含字母、数字和下划线，段之间可用 `/` 分隔。CAD 导出的特殊字符 link 名需要先整理为此规则，再核对关节引用。

## 与相机、雷达、IMU 的分工

传感器及其数据流可以引用本资产 URDF 中的非根 link。若 URDF 已通过 joint 发布某个安装 frame，则不要再配置同一传感器的 `transform`；重复 child 会在启动前被拒绝。[示例 IMU](../src/chaos_assets/models/demo_ugv/v1/model.yaml) 的安装位置由 URDF 负责，因此没有独立静态变换配置。

Gemini 335 的建议归属是：整机 URDF 提供机体到相机根 frame 的安装变换，官方驱动继续提供相机内部标定 TF。当前相机配置的根为 `vehicle_001_front_camera_link`，整机模型输出必须与它精确一致；不能仅增加一个名字相近的 `camera_link`。可以使用已经带完整 frame 名的 URDF 并设 `frame_prefix: ""`，也可以统一修改相机 `camera_name` 与模型命名规则。

由于当前默认框架前缀使用斜杠，原厂相机前缀使用 `camera_name + '_'`，接入时必须显式核对命名，而不是假定二者相同。原厂驱动的内部 TF 无法从一般资产配置中完整推断，导入完整厂家相机模型前仍需核对发布归属，避免与 `publish_tf: true` 同时发布相同 child。

## RViz 查看

另开终端加载同一 ROS 环境，并设置示例域号：

```bash
export ROS_DOMAIN_ID=87
export ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST
unset ROS_LOCALHOST_ONLY
rviz2
```

将 Fixed Frame 设置为 `demo_001/base_link`，添加 RobotModel，选择 `/robots/demo_001/robot_description`，描述话题使用 transient-local QoS。RobotModel 的 **TF Prefix 填 `demo_001`，不带最后的 `/`**；RSP 配置则为 `demo_001/`。RViz 会自行添加斜杠，见 [对应实现](https://github.com/ros2/rviz/blob/jazzy/rviz_default_plugins/src/rviz_default_plugins/robot/tf_link_updater.cpp#L58-L63)。

这份示例没有定位系统，所以两台机器人没有共同的 world/map 根；查看第二台时可切换 Fixed Frame。要在一幅场景中显示多台机器人，需要由仿真器或定位系统提供它们到共同坐标系的真实变换。
