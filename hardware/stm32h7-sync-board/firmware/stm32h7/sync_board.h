/* sync_board.h — STM32H7 同步板对外接口
 *
 * 设计要点（详见 hardware/stm32h7-sync-board/README.md）：
 *   TIM5  = PPS 捕获 + 1 µs 时基（slave reset on TI1，TRGO=Update，1 Hz）
 *   TIM2/3/4/1 = 四路触发输出（各自 slave reset on ITR0 ← TIM5_TRGO，
 *                                    PWM 模式，硬件产生边沿）
 *   所有通道频率必须整除 1 s，否则每秒重新锚定时会出现相位跳变。
 */
#ifndef SYNC_BOARD_H
#define SYNC_BOARD_H

#include <stdbool.h>
#include <stdint.h>

#include "sync_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SYNC_CH_CAMERA 0u /* 可见光相机 */
#define SYNC_CH_LIDAR 1u  /* 雷达 */
#define SYNC_CH_IMU 2u    /* IMU */
#define SYNC_CH_IR 3u     /* 红外相机 */
#define SYNC_N_CHANNELS_CORE SYNC_N_CHANNELS

/* 上电后的时钟状态 */
typedef enum {
  SYNC_STATE_INIT = 0,
  SYNC_STATE_WAIT_PPS,   /* 还没收到第一个 PPS */
  SYNC_STATE_LOCKED,     /* PPS 稳定，时钟可信 */
  SYNC_STATE_HOLDOVER     /* 丢失 PPS，晶振自由运行（降级） */
} sync_clock_state_t;

typedef struct {
  sync_clock_state_t state;
  uint32_t pps_seq;       /* 已捕获的 PPS 计数 */
  uint32_t pps_missed;    /* 丢失的 PPS 数 */
  uint32_t jitter_ns;     /* 最近一次 PPS 相对理想网格的偏差 */
  uint32_t jitter_avg_ns; /* 平均偏差 */
  uint32_t jitter_max_ns;
  uint64_t utc_ns;        /* 当前 UTC（GNSS/RTC 给出） */
  bool utc_valid;
  bool rtc_valid;
} sync_clock_info_t;

/* 初始化：配置四路触发的定时器与 GPIO。必须在 CubeMX 生成的
 * MX_TIMx_Init() 之后调用。 */
void sync_board_init(void);

/* PPS 捕获中断里调用（TIM5 CC1 中断） */
void sync_board_on_pps_capture(uint32_t counter);

/* 1 Hz 更新中断里调用（TIM5 update） */
void sync_board_on_pps_tick(void);

/* 每毫秒调用一次（SysTick 或 TIM6），更新状态老化 */
void sync_board_on_1ms_tick(void);

/* 应用层：配置某一路触发 */
bool sync_board_set_channel(uint8_t ch, const sync_channel_cfg_t *cfg);

/* 应用层：读时钟状态 */
void sync_board_get_clock_info(sync_clock_info_t *out);

/* 应用层：把当前状态编码成 status 帧 */
size_t sync_board_encode_status(uint8_t *buf, size_t cap);

/* 应用层：处理主机下发的配置帧 */
bool sync_board_handle_frame(const uint8_t *payload, size_t len);

#ifdef __cplusplus
}
#endif
#endif /* SYNC_BOARD_H */
