# 绍兴全图与 Go2 仿真

来源：`/home/yunze/绍兴全图.pcd`，压缩 PCD，**24,000,000 个有效 RGB 点**。
显示使用全部原始点，保持原始顺序和颜色，没有体素降采样、点数上限或按帧率抽点。
地图原始范围约 409 × 427 m，高度 4.55–63.35 m。

## 启动与操作

在工作区运行：

```bash
bash scripts/run_shaoxing_go2.sh
```

启动 GPU 高斯表面融合、原生 MuJoCo 物理和 Go2 行走策略。默认 ROS domain 为 **199**，
与原庭院场景的 198 隔离。该入口使用 `generated/shaoxing_go2/`；原庭院缓存继续保留。
全图物理模型启动约需一分钟；查看器标题会显示 `Waiting for physics`，收到反馈后进入场景。

- **W/S** 前后，**A/D** 转向，**Q/E** 横移；松开按键或窗口失焦时停止。
- **空格** 停止，**R** 复位，**F** 跟随机器狗，**O** 查看全图。
- **G** 切换高斯融合与圆片显示，**+ / −** 调整面片覆盖。
- 左键拖动旋转，右键/中键平移，滚轮缩放。

关闭查看器会停止这次启动的仿真和控制器。只运行物理与策略可加 `--headless`。
详细控制方式、策略来源和速度限制见 [通用说明](pcd-go2-scene.md)。

## 数据与物理

出生点在平坦步道，原 PCD 坐标为 `(0, 77, 15.7283000946)` m，初始朝向 +X。
1 m 内地面高度的 5%–95% 分位差约 7.9 mm，周围约 7.8 × 3 m 范围具有连续地面支持。
局部坐标由原始坐标减去出生点得到。PCD 的地理 `VIEWPOINT` 单独记录，不重复叠加。

GPU 属性包含位置/颜色 384 MB、法向 288 MB、半径 96 MB，合计约 732 MiB，
此外还有帧缓冲和驱动开销。高斯表面融合没有照片训练，原始扫描空洞仍会保留。

物理使用独立的 0.6 m 碰撞代理。638,803 个障碍占据体素被无损合并成 132,284 个长方体；
占据集合、空洞及外部边界保持一致。地面保留 70,895 个测量高度格，共 203,179 个静态碰撞体。
这些代理数量与显示点数无关。碰撞地面沿用出生高度 ±0.18 m 的地面识别方式；
其他高度使用占据方块，远处坡面、台阶和植被接触仍受 0.6 m 代理精度限制。

`go2_viewer.xml` 只供 GPU 查看器绘制机器人，关节布局与 `go2_physics.xml` 一致；
物理节点使用含完整碰撞代理的后者。这样查看器无需重复加载不可见的碰撞地图。

## 重新生成

```bash
python3 scripts/prepare_pcd_go2.py /home/yunze/绍兴全图.pcd \
  --model-root work/multi-model-team/simplify-20261031/model-cache \
  --output generated/shaoxing_go2 --spawn-xy 0 77 \
  --gpu-only --merge-collision-boxes
python3 scripts/prepare_pcd_gpu.py --scene-dir generated/shaoxing_go2 --voxel 0
python3 scripts/prepare_pcd_normals.py --scene-dir generated/shaoxing_go2
```

压缩读取器使用本机 libpcl_io 或 liblzf 解压，逐字段校验转换前后的原始字节，
不修改源 PCD。解压缓存位于 `/tmp/chaos-pcd-cache/`；GPU 缓存命中时只读取源文件头和状态。
法向计算采用磁盘空间分块与自适应邻域搜索，保留全局 12 近邻，逐点输出法向和半径。
本机新目录已具备 ONNX Runtime；其他环境的依赖安装方式见通用说明。

全点逐项坐标/颜色校验记录在 `generated/shaoxing_go2/full_points_validation.json`。
运行日志为该目录下的 `viewer.log`、`simulator.log` 和 `locomotion.log`。

本机 RTX 5060 在 1600×1000 下测得高斯模式近景约 65 FPS、全图约 25 FPS，实际随视角变化。
前进、双向转向、后退和指令超时停车复验通过，记录在 `locomotion_validation.json`。
完整碰撞地图负载较高，本次运动测试仿真时间约为墙钟时间的 84%，存在调度丢 tick；
策略能够行走，但当前结果不应解释为严格实时的控制性能测试。
