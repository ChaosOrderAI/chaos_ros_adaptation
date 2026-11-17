# CubeMX 配置清单

STM32H743VGTx（LQFP100）。按这份清单生成 `.ioc`，再把
`firmware/stm32h7/` 下的文件加进工程即可。

> 下面的引脚是**功能名**（PA0、TIM5_CH1 …），封装引脚号由 CubeMX 分配；
> `connections.md` 里的连接表与此一致。

## 1. 时钟

| 项 | 值 | 备注 |
|---|---|---|
| HSE | 晶振 25 MHz | Y1，22 pF 负载电容 |
| LSE | 晶振 32.768 kHz | Y2，6.8 pF |
| PLL1 | M=5, N=192, P=2, Q=4, R=2 | SYSCLK 480 MHz，VCO 960 MHz |
| D1CPRE | ÷1 | CPU 480 MHz |
| HPRE | ÷2 | AHB 240 MHz |
| PPRE1 | ÷2 | APB1 120 MHz |
| PPRE2 | ÷1 | APB2 120 MHz |

APB1/APB2 定时器时钟都是 **240 MHz**（APB 分频不为 1 时定时器 ×2）。
若 CubeMX 报 PLL VCO 超出范围，改 N=160 → SYSCLK 400 MHz，
定时器 200 MHz，把 `sync_trigger.c` 里的 `SYNC_TIMER_CLOCK_HZ` 改成 200000000 即可
（`sync_channel_plan()` 会自动重算 PSC/ARR）。

## 2. 定时器

| 定时器 | 用途 | PSC | ARR | 从模式 | 触发源 | 通道 |
|---|---|---|---|---|---|---|
| TIM5 (32 位) | PPS 捕获 + 1 s 锚点 | 23 | 99,999,999 | Reset | TI1（上升沿） | CH1 = Input Capture direct |
| TIM2 (32 位) | 触发 0：可见光相机 | 运行期由固件设置 | 同左 | Reset | ITR0 (TIM5_TRGO) | CH1 = PWM mode 1 |
| TIM3 (16 位) | 触发 1：雷达 | 同上 | 同上 | Reset | ITR0 | CH1 = PWM mode 1 |
| TIM4 (16 位) | 触发 2：IMU | 同上 | 同上 | Reset | ITR0 | CH1 = PWM mode 1 |
| TIM1 (16 位) | 触发 3：红外相机 | 同上 | 同上 | Reset | ITR0 | CH1 = PWM mode 1 |

- TIM5 的 TRGO = **Update**（每秒一次，正好落在 PPS 锚点）。
- TIM5 的 CC1 打开中断（捕获 PPS），同时开更新中断。
- 四个通道定时器的 PSC/ARR/CCR 由 `sync_trigger_configure()` 在运行时写入，
  CubeMX 里填一组默认值即可。

## 3. 串口与 I2C

| 外设 | 配置 | 用途 |
|---|---|---|
| USART1 | 921600 8N1，中断接收 1 字节 | 主机链路（帧协议） |
| USART2 | 115200 8N1，中断接收 1 字节 | RTK 的 NMEA |
| I2C1 | 100 kHz（DS3231 只支持标准模式） | DS3231 RTC |
| I2C2 | 400 kHz | AT24C02 配置存储 |

## 4. USB 与调试

- USB_OTG_FS：PA11/PA12，CDC。调试与固件升级，可选。
- SYS debug = Serial Wire，PA13/PA14。
- BOOT0 下拉 10 kΩ + 按键。

## 5. NVIC 优先级

| 中断 | 优先级 |
|---|---|
| TIM5 capture / update | 0（最高，PPS 边沿不能被推迟） |
| USART1 RX | 1 |
| USART2 RX | 1 |
| TIM2/3/4/1 update | 2 |

## 6. GPIO

- 四路触发输出：推挽输出，默认低，空闲态为低电平。
- PPS 捕获脚：PA0 配 AF2（TIM5_CH1），无内部上下拉（外部施密特整形器提供）。
- LED：PC0（PPS 锁定）、PC1（触发运行）、D3（故障）。

### 6.1 载板 mezzanine（J10）占用

当同步板作为 Orin NX 载板的 mezzanine 插上时，下面这些脚是**分配给载板**
的，不要挪作他用：

| 脚 | 功能 | 方向（同步板视角） | 说明 |
|---|---|---|---|
| PB1 | `MEZZ_TRIG_EN` | 输入 | 载板软件使能硬件触发 |
| PB2 / PB3 / PB4 | `MEZZ_AUX1..3` | 双向 | 备用 IO |
| PB5 | `MEZZ_AUX4` | 双向 | |
| PB7 | `MEZZ_AUX5` | 双向 | |
| PA4 / PA5 | `MEZZ_AUX6/7` | 双向 | |
| PC2 | `MEZZ_PPS_LOCKED` | 输出 | 回报 PPS 锁定状态 |
| PC3 | `MEZZ_TRIG_STAT` | 输出 | 触发运行状态 |
| PC4 | `MEZZ_CAM_VSYNC` | 输入 | 相机 VSYNC 回读 |
| PC5 | `MEZZ_CAM_TIMER` | 输入 | 相机 TIMER_SYNC 回读 |
| USART1 (PA9/PA10) | 主机链路 | 双向 | 走 J10.13/12（TTL 3.3 V） |

**装成 mezzanine 时，U3（RS-485 收发器）不贴**——USART1 同时接了 RS-485
（J3）和 TTL（J10），两个都是驱动源会打架。二选一：走 RS-485 就贴 U3、
J10.12/13 悬空；走载板就 DNP U3。

引脚分配与载板 J12 的对应关系由
`hardware/orin-nx-carrier/tools/check_mezzanine.py` 校验。
