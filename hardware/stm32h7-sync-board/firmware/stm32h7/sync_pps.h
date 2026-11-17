/* sync_pps.h */
#ifndef SYNC_PPS_H
#define SYNC_PPS_H

#include <stdbool.h>
#include <stdint.h>

#include "sync_board.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  sync_clock_state_t state;
  uint32_t seq;
  uint32_t missed;
  uint32_t interval_ticks; /* 上两次 PPS 的间隔（10 ns 为单位） */
  int32_t jitter_ns;
  int32_t jitter_avg_ns;
  int32_t jitter_max_ns;
  uint32_t good_streak;
} sync_pps_state_t;

void sync_pps_reset(void);

/* TIM5 捕获中断里调用，返回 true 表示这次 PPS 有效 */
bool sync_pps_on_capture(uint32_t ccr_value);

const sync_pps_state_t *sync_pps_state(void);

#ifdef __cplusplus
}
#endif
#endif /* SYNC_PPS_H */
