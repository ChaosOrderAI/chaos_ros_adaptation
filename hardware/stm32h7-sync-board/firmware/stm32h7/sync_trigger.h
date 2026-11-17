/* sync_trigger.h */
#ifndef SYNC_TRIGGER_H
#define SYNC_TRIGGER_H

#include "sync_board.h"
#include "sync_core.h"

#ifdef __cplusplus
extern "C" {
#endif

extern sync_channel_cfg_t g_sync_channels[SYNC_N_CHANNELS];
extern sync_timer_plan_t g_sync_plans[SYNC_N_CHANNELS];

/* 写某一路触发的周期/脉宽（不改变硬件已运行的通道前先调用 configure） */
bool sync_trigger_configure(uint8_t ch, const sync_channel_cfg_t *cfg);

/* 启动所有通道定时器 */
bool sync_trigger_start(void);

/* 当前启用的通道位图 */
uint32_t sync_trigger_active_mask(void);

#ifdef __cplusplus
}
#endif
#endif /* SYNC_TRIGGER_H */
