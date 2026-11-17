/* sync_core.h — 同步板可移植核心逻辑
 *
 * 这里只放与硬件无关的纯 C99 逻辑：协议编解码、触发相位调度、脉冲指纹、
 * GPS/UTC 时间换算。STM32 固件和主机端测试共用同一份实现，
 * 保证板上的行为可以在 PC 上复现和验证。
 */
#ifndef SYNC_CORE_H
#define SYNC_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------- 帧协议 */
/* 帧格式: A5 5A | ver | type | len(u16, LE) | payload | crc16(u16, LE) */

#define SYNC_PROTOCOL_VERSION 1u
#define SYNC_FRAME_HEADER0 0xA5u
#define SYNC_FRAME_HEADER1 0x5Au
#define SYNC_FRAME_OVERHEAD 9u /* 2+1+1+2+2 */
#define SYNC_FRAME_MAX 64u

typedef enum {
  SYNC_MSG_STATUS = 0x01,  /* 板上 → 主机: 状态与时间 */
  SYNC_MSG_PPS = 0x02,     /* 板上 → 主机: PPS 事件 */
  SYNC_MSG_TRIGGER = 0x03, /* 板上 → 主机: 触发指纹 */
  SYNC_MSG_CONFIG = 0x04,  /* 主机 → 板上: 通道配置 */
  SYNC_MSG_ACK = 0x05      /* 板上 → 主机: 应答 */
} sync_msg_type_t;

/* status.flags 位定义 */
#define SYNC_FLAG_PPS_LOCKED 0x01u
#define SYNC_FLAG_RTC_OK 0x02u
#define SYNC_FLAG_TRIGGER_RUNNING 0x04u
#define SYNC_FLAG_EXT_PPS 0x08u
#define SYNC_FLAG_CLOCK_STEP 0x10u

typedef enum {
  SYNC_CLOCK_FREE = 0, /* 自由振荡，未锁定 */
  SYNC_CLOCK_RTC = 1,  /* DS3231 驯服 */
  SYNC_CLOCK_GNSS = 2  /* RTK/GNSS PPS 驯服 */
} sync_clock_source_t;

typedef struct {
  uint64_t utc_ns;        /* 当前 UTC 纳秒（GPS 时刻换算后） */
  uint32_t pps_seq;       /* 自上电以来的 PPS 计数 */
  uint16_t pps_age_ms;    /* 距最近一次 PPS 的毫秒数 */
  int32_t jitter_ns;      /* 最近一次 PPS 相对理想网格的偏差 */
  uint8_t flags;          /* SYNC_FLAG_* */
  uint8_t channel_mask;   /* 已启用的触发通道位图 */
  uint8_t clock_source;   /* sync_clock_source_t */
  uint8_t rsv;
} sync_status_t;

uint16_t sync_crc16(const uint8_t *data, size_t len);

/* 序列化 status 成 payload（返回写入字节数） */
size_t sync_status_encode(const sync_status_t *status, uint8_t *out, size_t cap);
/* 反序列化 status payload */
bool sync_status_decode(const uint8_t *in, size_t len, sync_status_t *out);

/* 组帧：返回总帧长，容量不足返回 0 */
size_t sync_frame_build(uint8_t *buf, size_t cap, sync_msg_type_t type,
                        const uint8_t *payload, size_t payload_len);
/* 拆帧：成功返回 true 并回填 type/payload（payload 指向 buf 内部） */
bool sync_frame_parse(const uint8_t *buf, size_t len, sync_msg_type_t *type,
                      const uint8_t **payload, size_t *payload_len);

/* ------------------------------------------------------------ 触发通道 */
/* 通道配置：一路触发 = 一个定时器输出比较通道 */
typedef struct {
  uint32_t period_us; /* 触发周期（微秒） */
  uint32_t width_us;  /* 脉冲宽度（微秒） */
  uint32_t phase_us;  /* 相对 PPS 边界的相位偏移（微秒），< period_us */
  bool enabled;
} sync_channel_cfg_t;

#define SYNC_N_CHANNELS 4u

/* 从 PPS 边沿起算的第 us_since_pps 微秒，下一次触发沿还有多久（微秒）。
 * 通道未使能或周期为 0 时返回 UINT32_MAX。 */
uint32_t sync_channel_next_edge_us(const sync_channel_cfg_t *cfg,
                                    uint32_t us_since_pps);

/* 给定定时器计数值 counter，输出比较寄存器该写什么（相对 counter 的偏移）。
 * 溢出时用 mod 运算保证 CCR 在 [1, ARR] 内。 */
uint32_t sync_channel_oc_compare(const sync_channel_cfg_t *cfg,
                                  uint32_t counter, uint32_t arr);

/* 校验配置是否自洽：脉宽 < 周期、相位 < 周期 */
bool sync_channel_cfg_valid(const sync_channel_cfg_t *cfg);

/* 把 (周期, 脉宽) 换算成定时器寄存器值。
 * ARR <= 65535（16 位定时器上限），必要时用预分频把计数周期拉粗。 */
typedef struct {
  uint32_t psc;     /* 预分频寄存器值 */
  uint32_t arr;     /* 自动重装值 */
  uint32_t ccr;     /* 捕获/比较寄存器值 */
  uint32_t tick_ns; /* 计数周期（纳秒） */
  bool ok;
} sync_timer_plan_t;

bool sync_channel_plan(const sync_channel_cfg_t *cfg, uint32_t timer_hz,
                       sync_timer_plan_t *plan);

/* ------------------------------------------------------------ 脉冲指纹 */
/* 前 N 个 PPS 用递增的脉冲宽度编码一个固定图案，事后据此把各路数据流
 * 对齐到同一条时间轴（丢触发后仍能靠指纹重新对齐）。 */
#define SYNC_FINGERPRINT_STEPS 6u
extern const uint8_t sync_fingerprint_default[SYNC_FINGERPRINT_STEPS];

void sync_fingerprint_widths_us(const uint8_t *code, uint32_t base_us,
                                uint32_t step_us,
                                uint32_t out_us[SYNC_FINGERPRINT_STEPS]);
bool sync_fingerprint_matches(const uint32_t *widths_us, const uint8_t *code,
                              uint32_t base_us, uint32_t step_us);

/* ------------------------------------------------------------ 时间换算 */
/* GPS 时刻自 1980-01-06 00:00:00 起连续计数，不含闰秒。
 * UTC = GPS - leap_seconds。Unix = GPS - GPS_TO_UNIX_OFFSET_S - leap_seconds */
#define SYNC_GPS_TO_UNIX_OFFSET_S 315964800u /* 1980-01-06 起的秒数 */
#define SYNC_LEAP_SECONDS_2017 37u

uint64_t sync_gps_ms_to_unix_ms(uint64_t gps_ms, uint32_t leap_seconds);
uint64_t sync_unix_ms_to_gps_ms(uint64_t unix_ms, uint32_t leap_seconds);

/* 理想 PPS 网格：给定 UTC 毫秒，算出相对 PPS 边界的偏移（纳秒），
 * 用于把测量到的 PPS 边沿抖动量化到整数微秒网格上。 */
int64_t sync_pps_grid_offset_ns(uint64_t unix_ms, uint64_t measured_unix_ms);

#ifdef __cplusplus
}
#endif
#endif /* SYNC_CORE_H */
