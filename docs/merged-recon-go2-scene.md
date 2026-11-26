# 绍兴重建网格与 Go2 仿真

来源：`/home/yunze/Downloads/Merged_Data_recon_final.ply`，Open3D 写出的二进制三角网格，
**10,789,741 个带 RGB 和法向的顶点、21,466,654 个三角形**，表面积约 232,862 m²，
范围约 336 × 385 m，高度 13.68–41.87 m。它与 [绍兴 LAS](shaoxing-las-go2-scene.md)
是同一场地、同一投影坐标系。

## 启动

```bash
bash scripts/run_merged_recon_go2.sh
```

默认 ROS domain 为 **201**，场景目录为 `generated/merged_recon_go2/`。
操作方式、多机参数、控制策略和速度限制与 [通用说明](pcd-go2-scene.md) 相同。
物理节点加载碰撞地图约需 90 秒，期间查看器标题显示等待 ROS 状态。

## 网格如何进入场景

现有流程显示和碰撞都基于彩色点，因此 `mesh_to_pcd.py` 把网格表面写成点：
全部顶点，加上每个三角形内按面积均匀采样的点（默认每平方米 150 个，固定随机种子），
采样点颜色是三角形顶点颜色的重心插值，也就是顶点着色网格本来显示的颜色。
共 **45,720,721 点**（10,789,741 顶点 + 34,930,980 采样点），不产生网格表面之外的点。
网格自带的顶点法向没有使用，显示法向仍由 `prepare_pcd_normals.py` 从点估计。

坐标沿用 LAS 场景的原点：`pcd_xyz = mesh_xyz - (554776.57705, 3312849.9003, 0)`，
所以出生点仍是 `(0, 77)`，两个场景的局部坐标可以直接对照。

碰撞仍是 0.6 m 的地面砖和占据体素长方体，不是三角网格碰撞。

## 重新生成

```bash
python3 scripts/mesh_to_pcd.py /home/yunze/Downloads/Merged_Data_recon_final.ply \
  --output generated/merged_recon_go2/source_points.pcd \
  --origin 554776.57705 3312849.9003 0
python3 scripts/prepare_pcd_go2.py generated/merged_recon_go2/source_points.pcd \
  --model-root ~/.cache/chaos/mujoco \
  --output generated/merged_recon_go2 --spawn-xy 0 77 \
  --gpu-only --merge-collision-boxes
python3 scripts/prepare_pcd_gpu.py --scene-dir generated/merged_recon_go2 --voxel 0
python3 scripts/prepare_pcd_normals.py --scene-dir generated/merged_recon_go2
```

`mesh_to_pcd.py` 只支持二进制小端、带顶点颜色的纯三角形 PLY，源文件以内存映射只读打开。
`--density` 调整每平方米采样数，`--density 0` 只写顶点；不给 `--origin` 时取顶点范围的 XY 中心。
本机转换约 40 秒，法向估计约 6 分钟。行走控制需要的 ONNX Runtime 通过
`generated/merged_recon_go2/python` 链接到绍兴全图场景的安装。

## 本机验证

GPU 查看器在 RTX 5060 上加载全部 45,720,721 点和 10,269,246 个三维高斯，
出生点跟随视角约 80–85 FPS，显存占用约 2.5 GB。
物理节点加载 178,723 个碰撞体（79,052 个地面格、99,671 个障碍长方体），出生点 1 m 内地面 5%–95% 高度差约 7 mm。

`0.3 m/s` 前进指令下机器人 5 秒内实际前进约 0.80 m，随后停车、原地左转均正常，没有摔倒。
自动化行走检查 `check_go2_locomotion.py` 没有通过：仿真时间只有墙钟时间的约 0.63–0.72，
在左转阶段低于检查要求而中止，后面的倒退阶段没有执行。
这与 [绍兴 LAS](shaoxing-las-go2-scene.md) 的情况相同，说明策略能够行走，
但不能当作实时控制性能的结论。
