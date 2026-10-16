# 硬件驱动工作空间

此目录保存原厂 ROS 2 驱动，独立于 `../src` 中的框架包构建。当前已加入奥比中光 Gemini 335 对应的官方 `OrbbecSDK_ROS2` v2.10.6 源码，以及随版本提供的 x64、ARM64 SDK。没有修改原厂源码。

- 版本清单：[orbbec.repos](orbbec.repos)，固定 commit `296156521b60bdf813cb94c46cbb55562bbdc453`。
- 源码：[src/OrbbecSDK_ROS2](src/OrbbecSDK_ROS2)。完整保留 `LICENSE`、`NOTICE` 和 SDK 第三方许可证。
- 构建入口：`./build-orbbec.sh`，只构建 `orbbec_camera` 及其消息依赖，输出到本目录 `build/install/log`。
- 配置、缺失依赖和验收说明：[奥比中光驱动接入](../docs/orbbec-driver.md)。

`colcon build --base-paths src` 在 `chaos_ws` 根目录执行时不会构建这里的原厂包；在本目录执行时只扫描本目录的驱动源码。业务框架测试无需相机 SDK 或 USB 设备。

重新获取源码时，在空的本目录 `src` 下执行 `vcs import src < orbbec.repos`；已有本地修改时先检查差异，不要直接覆盖。升级时更新清单中的精确 commit，并重新验证原厂 SDK、资产模板和实际硬件。
