/* main_user.c — CubeMX 工程里的用户代码
 *
 * 用法：在 CubeMX 生成的工程里新建 USER CODE 段，把本文件的函数体搬进去，
 * 或者直接把本目录的文件加进工程并按下面对应关系接线。
 *
 * CubeMX 侧需要的配置见同目录 CUBE.md。
 */
#include "main.h"

#include "sync_board.h"
#include "sync_host.h"
#include "sync_pps.h"
#include "sync_time.h"
#include "sync_trigger.h"

extern TIM_HandleTypeDef htim1;
extern TIM_HandleTypeDef htim2;
extern TIM_HandleTypeDef htim3;
extern TIM_HandleTypeDef htim4;
extern TIM_HandleTypeDef htim5;
extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;

/* 默认通道配置：频率取 1 s 的整数分频，保证每秒重新锚定时不产生相位跳变 */
static const sync_channel_cfg_t k_default_channels[SYNC_N_CHANNELS] = {
    /* 可见光相机 25 Hz */ {25000u, 1000u, 0u, true},
    /* 雷达 10 Hz */ {100000u, 10000u, 0u, true},
    /* IMU 100 Hz */ {10000u, 500u, 0u, true},
    /* 红外相机 20 Hz */ {50000u, 1000u, 0u, true},
};

void sync_board_init(void) {
  uint8_t ch;
  sync_pps_reset();
  for (ch = 0; ch < SYNC_N_CHANNELS; ++ch) {
    (void)sync_trigger_configure(ch, &k_default_channels[ch]);
  }
  /* TIM5：PPS 捕获 + 1 s 锚点，需要开 CC1 中断与更新中断 */
  (void)HAL_TIM_Base_Start_IT(&htim5);
  (void)HAL_TIM_Base_Start(&htim5);
  (void)HAL_TIM_PWM_Start(&htim5, TIM_CHANNEL_1); /* 同时使能 CC1 捕获通道 */
  (void)sync_trigger_start();
  sync_host_init();
}

/* TIM5 捕获中断：PPS 上升沿 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
  if (htim->Instance == TIM5) {
    if (__HAL_TIM_GET_FLAG(htim, TIM_FLAG_CC1) != RESET) {
      __HAL_TIM_CLEAR_FLAG(htim, TIM_FLAG_CC1);
      (void)sync_pps_on_capture(__HAL_TIM_GET_COUNTER(htim));
    }
  }
}

/* USART1：主机链路 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
  if (huart->Instance == USART1) {
    sync_host_on_rx_isr();
  }
}

/* USART2：RTK 的 NMEA */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
  if (huart->Instance == USART2) {
    uint8_t byte = 0u;
    if (HAL_UART_Receive_IT(huart, &byte, 1u) == HAL_OK) {
      sync_time_nmea_push(byte);
    }
  }
}

/* SysTick 1 ms */
void sync_board_on_1ms_tick(void) { /* 预留：状态老化、喂狗 */ }

/* 1 Hz：由 TIM5 更新中断调用，统计秒边界 */
void sync_board_on_pps_tick(void) { /* 预留：指纹脉冲计数 */ }

void sync_board_on_pps_capture(uint32_t counter) {
  (void)sync_pps_on_capture(counter);
}

bool sync_board_set_channel(uint8_t ch, const sync_channel_cfg_t *cfg) {
  return sync_trigger_configure(ch, cfg);
}

void sync_board_get_clock_info(sync_clock_info_t *out) {
  const sync_pps_state_t *pps;
  if (out == NULL) {
    return;
  }
  pps = sync_pps_state();
  out->state = pps->state;
  out->pps_seq = pps->seq;
  out->pps_missed = pps->missed;
  out->jitter_ns = (uint32_t)pps->jitter_ns;
  out->jitter_avg_ns = (uint32_t)pps->jitter_avg_ns;
  out->jitter_max_ns = (uint32_t)pps->jitter_max_ns;
  out->utc_valid = false;
  out->rtc_valid = sync_time_state()->rtc_valid;
  {
    uint64_t unix_ms = 0u;
    if (sync_time_gnss_utc_ms(&unix_ms)) {
      out->utc_ns = unix_ms * 1000000ull;
      out->utc_valid = true;
    } else if (sync_time_read_rtc(&unix_ms)) {
      out->utc_ns = unix_ms * 1000000ull;
    }
  }
}

size_t sync_board_encode_status(uint8_t *buf, size_t cap) {
  sync_status_t status;
  sync_clock_info_t info;
  uint8_t payload[32];
  size_t n;

  sync_board_get_clock_info(&info);
  memset(&status, 0, sizeof(status));
  status.utc_ns = info.utc_ns;
  status.pps_seq = info.pps_seq;
  status.pps_age_ms = 0u;
  status.jitter_ns = (int32_t)info.jitter_ns;
  status.flags = 0u;
  if (info.state == SYNC_CLOCK_STATE_LOCKED) {
    status.flags |= SYNC_FLAG_PPS_LOCKED;
  }
  if (info.rtc_valid) {
    status.flags |= SYNC_FLAG_RTC_OK;
  }
  if (sync_trigger_active_mask() != 0u) {
    status.flags |= SYNC_FLAG_TRIGGER_RUNNING;
  }
  status.channel_mask = (uint8_t)sync_trigger_active_mask();
  status.clock_source = (info.state == SYNC_CLOCK_STATE_LOCKED)
                            ? (uint8_t)SYNC_CLOCK_GNSS
                            : (uint8_t)SYNC_CLOCK_FREE;

  n = sync_status_encode(&status, payload, sizeof(payload));
  if (n == 0u) {
    return 0u;
  }
  return sync_host_send((uint8_t)SYNC_MSG_STATUS, payload, n);
}

bool sync_board_handle_frame(const uint8_t *payload, size_t len) {
  if (payload == NULL || len < 9u) {
    return false;
  }
  /* CONFIG: ch(1) period_us(4) width_us(4) flags(1) */
  {
    sync_channel_cfg_t cfg;
    uint8_t ch = payload[0];
    cfg.period_us = (uint32_t)payload[1] | ((uint32_t)payload[2] << 8) |
                    ((uint32_t)payload[3] << 16) | ((uint32_t)payload[4] << 24);
    cfg.width_us = (uint32_t)payload[5] | ((uint32_t)payload[6] << 8) |
                   ((uint32_t)payload[7] << 16) | ((uint32_t)payload[8] << 24);
    cfg.phase_us = 0u;
    cfg.enabled = (len > 9u) ? ((payload[9] & 0x01u) != 0u) : true;
    if (!sync_board_set_channel(ch, &cfg)) {
      uint8_t ack[2] = {ch, 0u};
      (void)sync_host_send((uint8_t)SYNC_MSG_ACK, ack, sizeof(ack));
      return false;
    }
    {
      uint8_t ack[2] = {ch, 1u};
      (void)sync_host_send((uint8_t)SYNC_MSG_ACK, ack, sizeof(ack));
    }
  }
  return true;
}
