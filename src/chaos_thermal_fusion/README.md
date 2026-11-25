# chaos_thermal_fusion

Thermal/visible fusion for the chaos platform.

## Nodes

| node | what it does |
|---|---|
| `at600_split_node` | **Moved to `chaos_sensors`** (sensor acquisition lives in the umbrella package). `thermal_fusion.launch.py` still starts it, together with the fusion node below. |
| `thermal_register_calib` | Estimates the visible -> thermal homography from a live image pair and writes it as a ROS 2 parameter file. |
| `thermal_fusion_node` | Fuses any two image topics into `thermal/image_fused` (`bgr8`). Optional `homography` warps the visible image into the thermal frame first. |

## Run (AT600)

```bash
ros2 launch chaos_thermal_fusion thermal_fusion.launch.py
# mode:=blend|detail|side_by_side|thermal_color  alpha:=0.6  colormap:=inferno
```

The AT600 halves are registered by the module itself, so no homography is
needed for them: `homography: []` means "resize only".

## Registering a separate visible camera

With a separately mounted camera (Hikvision USB/RTSP, etc.) the images are not
pixel-aligned; estimate the homography once:

```bash
ros2 run chaos_thermal_fusion thermal_register_calib --ros-args \
  -p thermal_topic:=/thermal/image_raw \
  -p visible_topic:=/hikvision/image_raw \
  -p output_file:=/tmp/thermal_homography.yaml \
  -p preview_file:=/tmp/thermal_registration.png
```

It matches SIFT features across the two modalities, fits a RANSAC homography,
prints it, and exits. Check the preview (thermal = red channel, warped visible =
green); misalignment shows as red/green fringes. Then fuse:

```bash
ros2 run chaos_thermal_fusion thermal_fusion_node --ros-args \
  --params-file /tmp/thermal_homography.yaml \
  -p thermal_topic:=/thermal/image_raw \
  -p visible_topic:=/hikvision/image_raw \
  -p mode:=detail
```

The params file keys on the node name `thermal_fusion_node`; if the node is
renamed, pass `-p node_name:=<new name>` to the calibration node when writing.

Notes:

- A homography handles rotation + translation + scale of a planar scene, which
  is the right model for a forward-looking camera pair. It degrades for strong
  parallax (large baseline).
- Re-run the calibration whenever a camera is remounted.
- `ratio_test` defaults to 0.85 (cross-modal matching needs it looser than the
  usual 0.75); raise `min_inliers` if the scene is cluttered.
- Validate on the AT600 halves first: the estimate should come back close to
  identity (measured: ~1.02/0.96 scale, a few pixels of translation).

## Fusion modes

- `blend` -- `alpha * colormap(thermal) + (1 - alpha) * CLAHE(visible)`
- `detail` -- colormap(thermal) + high-frequency detail from the visible image
- `side_by_side` -- colormap | visible, for calibration and debugging
- `thermal_color` -- colormap only

Thermal normalization uses a 2%/98% percentile stretch unless `thermal_min` and
`thermal_max` are both set.
