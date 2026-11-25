# chaos_sensors

The single sensor management package of the chaos platform.

It owns everything the platform itself maintains on the sensor side:

| executable | role |
|---|---|
| `mock_sensor` | Synthetic streams for `mode: mock` assets; never touches hardware |
| `sensor_monitor` | Per-asset stream health, diagnostics and `sensors/description` |
| `at600_split_node` | IRay AT600 dual-spectrum capture (raw V4L2, kernel timestamps) and left/right split |
| `trigger_pair_node` | Pairs externally triggered camera frames with the sync board's trigger edges and republishes them stamped with the trigger time |
| `chaos_time_sync_node` | Sync board client: PTP-like clock exchange, trigger/IMU reports, `/board/*` topics and diagnostics |

## Layout

```
chaos_sensors/
├── include/chaos_sensors/     shared headers: health, sensor config, QoS, stamps
├── src/                       the executables above
├── config/                    per-node parameter defaults
├── launch/                    time_sync.launch.py, trigger_pair.launch.py
├── docs/                      time_sync.md, trigger_pair.md (protocol, wiring, diagnostics)
├── vendor/                    third-party drivers, each its own colcon package (see vendor/README.md)
└── catalog/                   one entry per supported sensor model (see catalog/sensors.yaml)
```

## Maintenance rules

- **Our acquisition code lives here.** A new first-party sensor node is a new
  executable in this package, not a new top-level package.
- **Third-party drivers stay separate packages under `vendor/`.** They keep
  their own `package.xml`, LICENSE and upstream history; this package pulls
  them in with `exec_depend` so `ros2 pkg` and the bringup see one entry
  point. `vendor/manifest.yaml` pins URL + commit + license + local patches.
- **Updates are manifest-driven.** `scripts/update_vendor.py <name>` fetches the
  pinned revision, re-applies local patches, rebuilds that driver and runs its
  tests. `scripts/check_vendor.py` verifies every vendored tree matches the
  manifest before a commit.
- **The catalog is the device table.** `catalog/sensors.yaml` names every
  supported model, its driver (ours or vendored/apt), the stream contract and
  the `time_sync` defaults. Adding a sensor is a catalog entry plus, when
  needed, one node.

## Documentation

- [docs/time_sync.md](docs/time_sync.md) — sync board protocol, wiring, diagnostics
- [docs/trigger_pair.md](docs/trigger_pair.md) — trigger pairing, Hikvision setup
- [../../docs/sensors.md](../../docs/sensors.md) — asset-side sensor contract (`time_sync`, `hosts.board`)
