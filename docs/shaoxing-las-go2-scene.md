# 绍兴 LAS 全量测绘与 Go2 仿真

来源：`/home/yunze/Downloads/Merged Data.las`，LAS 1.2、点格式 3，**332,019,490 个 RGB 点**，
范围约 409 × 427 m，高度 4.55–63.35 m。它与 [绍兴全图](shaoxing-go2-scene.md) 是同一场地，
后者是 2400 万点的抽稀版本。

## 为什么要抽稀

全部点的显示缓冲约需 10.6 GB（位置/颜色 16 字节、法向 12 字节、半径 4 字节），
超过本机 8 GB 显存，因此导入时按体素抽稀。每个占据体素保留源文件中的第一个实测点，
不做平均，也不生成新点。本场景使用 **4 cm 体素，保留 63,352,421 点**。

| 体素 | 保留点数 |
|---|---|
| 1 cm | 236,681,168 |
| 2 cm | 140,608,863 |
| 3 cm | 90,829,005 |
| 4 cm | 63,352,421 |
| 5 cm | 46,792,384 |
| 7.5 cm | 25,825,451 |
| 10 cm | 16,406,028 |

## 启动

```bash
bash scripts/run_shaoxing_las_go2.sh
```

默认 ROS domain 为 **200**，场景目录为 `generated/shaoxing_las_go2/`。
操作方式、控制策略和速度限制与 [通用说明](pcd-go2-scene.md) 相同。
`bash scripts/run_shaoxing_las_go2.sh --robots 3` 把多台 Go2 放进同一份碰撞场景；
这张图的物理本来就慢于实时，机器人变多后会更重。可选 `--layout`、
`--spacing`、`--base-name`、`--namespaces` 和 `--policy`，见
[通用说明](pcd-go2-scene.md#多机)。

## 坐标

LAS 使用投影坐标，float32 无法以毫米精度保存，因此转换时减去原点：
`pcd_xyz = las_xyz - (554776.57705, 3312849.9003, 0)`，即文件头范围的 XY 中心，高度不变。
原点写入 PCD 的 `VIEWPOINT` 和 `source_points.json`。
这个局部坐标系与绍兴全图 PCD 一致，所以沿用同一个出生点 `(0, 77)`。

## 重新生成

```bash
python3 scripts/las_to_pcd.py "/home/yunze/Downloads/Merged Data.las" \
  --output generated/shaoxing_las_go2/source_points.pcd --target-points 64000000
python3 scripts/prepare_pcd_go2.py generated/shaoxing_las_go2/source_points.pcd \
  --model-root ~/.cache/chaos/mujoco \
  --output generated/shaoxing_las_go2 --spawn-xy 0 77 \
  --gpu-only --merge-collision-boxes
python3 scripts/prepare_pcd_gpu.py --scene-dir generated/shaoxing_las_go2 --voxel 0
python3 scripts/prepare_pcd_normals.py --scene-dir generated/shaoxing_las_go2
```

`las_to_pcd.py` 不修改源文件，也不整体载入内存：先按 32 m 分块写到输出目录旁的临时目录
（约 6 GB，结束后删除），再逐块抽稀写出。`--target-points` 会统计上表各体素的点数，
选用不超过上限的最细体素；`--voxel` 直接指定体素，`--voxel 0` 保留全部点。
只支持未压缩且带 RGB 的 LAS（点格式 2、3、5、7、8、10），不支持 LAZ。
本机转换耗时约 7 分钟。行走控制需要的 ONNX Runtime 通过
`generated/shaoxing_las_go2/python` 链接到绍兴全图场景的安装。

## 本机验证

GPU 查看器在 RTX 5060、1600×1000 下加载全部 63,352,421 点。
逐点高斯模式全图约 7 FPS、近景约 25 FPS。
三维高斯细节层次把远处换成 7,496,358 个协方差高斯后，
同一视角全图约 33 FPS、近景约 108 FPS，近处仍是原始点。
截图为 `full_scene_3dgs.png` 和 `go2_closeup_3dgs.png`。
物理节点加载 202,922 个碰撞体（70,776 个地面格、132,146 个障碍长方体），
出生点 1 m 内地面 5%–95% 高度差约 8 mm。

`0.3 m/s` 前进指令下机器人 4 秒内实际前进约 0.60 m。
自动化行走检查 `check_go2_locomotion.py` 没有通过：仿真时间只有墙钟时间的约 0.66，
低于检查要求的 0.70，settle 阶段 3 秒内丢弃约 459 个物理 tick。
碰撞地图规模与 [绍兴全图](shaoxing-go2-scene.md) 相同，该场景此前测得约 0.85；
本次机器上同时运行着其他程序。这个结果说明策略能够行走，
但不能当作实时控制性能的结论。
