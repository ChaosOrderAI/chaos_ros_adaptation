/* sync_pps.c — GNSS/RTK PPS 捕获与时钟锁定
 *
 * TIM5 通道 1 做输入捕获，slave 模式设为 Reset：每个 PPS 上升沿把计数器
 * 清零，于是 CCR1 里存的就是"距上一个 PPS 的时间间隔"。
 * 计数 tick 取 10 ns（PSC=23 @240 MHz，TIM5 是 32 位，ARR=99,999,999 = 1 s），
 * 所以 PPS 间隔误差可以直接换算成 ns 量级的抖动。
 */
#include "sync_pps.h"

#include "main.h"

extern TIM_HandleTypeDef htim5;

#define SYNC_PPS_TICK_NS 10u
#define SYNC_PPS_PERIOD_TICKS 100000000u /* 1 s */
#define SYNC_PPS_TOLERANCE_TICKS 100000u /* 1 ms */
#define SYNC_PPS_LOCK_SAMPLES 3u

static sync_pps_state_t g_pps;

void sync_pps_reset(void) {
  g_pps.seq = 0u;
  g_pps.missed = 0u;
  g_pps.interval_ticks = 0u;
  g_pps.jitter_ns = 0;
  g_pps.jitter_avg_ns = 0;
  g_pps.jitter_max_ns = 0;
  g_pps.state = SYNC_CLOCK_STATE_WAIT_PPS;
  g_pps.good_streak = 0u;
}

bool sync_pps_on_capture(uint32_t ccr_value) {
  uint32_t interval = ccr_value;
  int32_t jitter_ns;
  bool good;

  g_pps.seq++;

  if (g_pps.state == SYNC_CLOCK_STATE_WAIT_PPS) {
    /* 第一次捕获只用来建立基准 */
    g_pps.interval_ticks = interval;
    g_pps.state = SYNC_CLOCK_STATE_LOCKED;
    g_pps.good_streak = 1u;
    return true;
  }

  good = (interval > (SYNC_PPS_PERIOD_TICKS - SYNC_PPS_TOLERANCE_TICKS)) &&
         (interval < (SYNC_PPS_PERIOD_TICKS + SYNC_PPS_TOLERANCE_TICKS));

  if (!good) {
    g_pps.missed++;
    g_pps.good_streak = 0u;
    g_pps.state = SYNC_CLOCK_STATE_HOLDOVER;
    g_pps.interval_ticks = interval;
    return false;
  }

  g_pps.interval_ticks = interval;
  jitter_ns = (int32_t)((int64_t)(interval - SYNC_PPS_PERIOD_TICKS) *
                         (int64_t)SYNC_PPS_TICK_NS);
  g_pps.jitter_ns = jitter_ns;

  /* 滑动平均，避免单次野值带偏 */
  if (g_pps.good_streak == 0u) {
    g_pps.jitter_avg_ns = jitter_ns;
  } else {
    g_pps.jitter_avg_ns = (int32_t)((g_pps.jitter_avg_ns * 7 + jitter_ns) / 8);
  }
  if ((jitter_ns > 0 ? jitter_ns : -jitter_ns) >
      (g_pps.jitter_max_ns > 0 ? g_pps.jitter_max_ns : -g_pps.jitter_max_ns)) {
    g_pps.jitter_max_ns = jitter_ns;
  }

  g_pps.good_streak++;
  if (g_pps.good_streak >= SYNC_PPS_LOCK_SAMPLES &&
      g_pps.state != SYNC_CLOCK_STATE_LOCKED) {
    g_pps.state = SYNC_CLOCK_STATE_LOCKED;
  }
  return true;
}

const sync_pps_state_t *sync_pps_state(void) { return &g_pps; }
