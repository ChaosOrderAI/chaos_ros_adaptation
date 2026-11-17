/* sync_trigger.c — 四路硬件触发输出
 *
 * 每路一个定时器通道，全部 slave reset 到 TIM5_TRGO（1 Hz，PPS 锚定），
 * PWM 模式产生脉冲：边沿由硬件比较单元产生，抖动只有定时器量化误差
 * （240 MHz 下约 4.2 ns）加输出驱动器延迟，与软件中断无关。
 *
 * 关键约束：通道周期必须整除 1 s（例如 10/20/25/50/100 Hz）。
 * 否则每秒的重新锚定会在通道上造成最多一个周期的相位跳变。
 * 需要 30 Hz 这类频率时，见 README 的"DMA 比较表"方案。
 */
#include "sync_trigger.h"

#include "main.h"

/* TIMx 句柄与通道，由 CubeMX 生成的外设句柄引用 */
extern TIM_HandleTypeDef htim1;
extern TIM_HandleTypeDef htim2;
extern TIM_HandleTypeDef htim3;
extern TIM_HandleTypeDef htim4;

/* 定时器时钟：APB1/APB2 定时器都是 240 MHz */
#define SYNC_TIMER_CLOCK_HZ 240000000u

static const sync_timer_plan_t *plan_of(uint8_t ch, TIM_HandleTypeDef **htim) {
  switch (ch) {
    case SYNC_CH_CAMERA:
      *htim = &htim2;
      return &g_sync_plans[SYNC_CH_CAMERA];
    case SYNC_CH_LIDAR:
      *htim = &htim3;
      return &g_sync_plans[SYNC_CH_LIDAR];
    case SYNC_CH_IMU:
      *htim = &htim4;
      return &g_sync_plans[SYNC_CH_IMU];
    case SYNC_CH_IR:
      *htim = &htim1;
      return &g_sync_plans[SYNC_CH_IR];
    default:
      *htim = NULL;
      return NULL;
  }
}

bool sync_trigger_configure(uint8_t ch, const sync_channel_cfg_t *cfg) {
  TIM_HandleTypeDef *htim = NULL;
  sync_timer_plan_t plan;
  TIM_OC_InitTypeDef oc = {0};
  const sync_timer_plan_t *stored;

  if (ch >= SYNC_N_CHANNELS || cfg == NULL) {
    return false;
  }
  if (!sync_channel_plan(cfg, SYNC_TIMER_CLOCK_HZ, &plan)) {
    return false;
  }
  stored = plan_of(ch, &htim);
  if (htim == NULL || stored == NULL) {
    return false;
  }

  /* 先停掉输出，改完寄存器再放行，避免改配置时产生毛刺 */
  (void)HAL_TIM_OC_Stop(htim, TIM_CHANNEL_1);

  htim->Instance->PSC = plan.psc;
  htim->Instance->ARR = plan.arr;
  __HAL_TIM_SET_COMPARE(htim, TIM_CHANNEL_1, plan.ccr);

  oc.OCMode = TIM_OCMODE_PWM1;
  oc.Pulse = plan.ccr;
  oc.OCPolarity = TIM_OCPOLARITY_HIGH;
  oc.OCFastMode = TIM_OCFAST_DISABLE;
  oc.OCIdleState = TIM_OCIDLESTATE_RESET;
  oc.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  oc.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_OC_ConfigChannel(htim, &oc, TIM_CHANNEL_1) != HAL_OK) {
    return false;
  }

  *stored = plan;
  g_sync_channels[ch] = *cfg;

  if (cfg->enabled) {
    if (HAL_TIM_OC_Start(htim, TIM_CHANNEL_1) != HAL_OK) {
      return false;
    }
  }
  return true;
}

bool sync_trigger_start(void) {
  uint8_t ch;
  for (ch = 0; ch < SYNC_N_CHANNELS; ++ch) {
    TIM_HandleTypeDef *htim = NULL;
    plan_of(ch, &htim);
    if (htim == NULL) {
      continue;
    }
    /* 每个通道定时器都 slave reset 到 TIM5_TRGO（ITR0），
     * 由 CubeMX 生成的 MX_TIMx_Init() 配好，这里只启动计数。 */
    if (HAL_TIM_Base_Start(htim) != HAL_OK) {
      return false;
    }
  }
  return true;
}

uint32_t sync_trigger_active_mask(void) {
  uint32_t mask = 0u;
  uint8_t ch;
  for (ch = 0; ch < SYNC_N_CHANNELS; ++ch) {
    if (g_sync_channels[ch].enabled) {
      mask |= (1u << ch);
    }
  }
  return mask;
}
