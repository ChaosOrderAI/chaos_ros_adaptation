/* sync_core.c — 见 sync_core.h */
#include "sync_core.h"

#include <string.h>

/* ---------------------------------------------------------------- CRC16 */
/* CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no xor-out */
uint16_t sync_crc16(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFFu;
  size_t i;
  int b;
  if (data == NULL) {
    return crc;
  }
  for (i = 0; i < len; ++i) {
    crc ^= (uint16_t)((uint16_t)data[i] << 8);
    for (b = 0; b < 8; ++b) {
      if (crc & 0x8000u) {
        crc = (uint16_t)((uint16_t)(crc << 1) ^ 0x1021u);
      } else {
        crc = (uint16_t)(crc << 1);
      }
    }
  }
  return crc;
}

/* ------------------------------------------------------------ 帧编解码 */
size_t sync_frame_build(uint8_t *buf, size_t cap, sync_msg_type_t type,
                        const uint8_t *payload, size_t payload_len) {
  size_t total = SYNC_FRAME_OVERHEAD + payload_len;
  uint16_t crc;
  if (buf == NULL || payload_len > (SYNC_FRAME_MAX - SYNC_FRAME_OVERHEAD)) {
    return 0;
  }
  if (cap < total) {
    return 0;
  }
  buf[0] = SYNC_FRAME_HEADER0;
  buf[1] = SYNC_FRAME_HEADER1;
  buf[2] = SYNC_PROTOCOL_VERSION;
  buf[3] = (uint8_t)type;
  buf[4] = (uint8_t)(payload_len & 0xFFu);
  buf[5] = (uint8_t)((payload_len >> 8) & 0xFFu);
  if (payload_len > 0u && payload != NULL) {
    memcpy(&buf[6], payload, payload_len);
  }
  crc = sync_crc16(&buf[2], 4u + payload_len);
  buf[6 + payload_len] = (uint8_t)(crc & 0xFFu);
  buf[7 + payload_len] = (uint8_t)((crc >> 8) & 0xFFu);
  return total;
}

bool sync_frame_parse(const uint8_t *buf, size_t len, sync_msg_type_t *type,
                      const uint8_t **payload, size_t *payload_len) {
  uint16_t plen, crc, calc;
  if (buf == NULL || len < SYNC_FRAME_OVERHEAD) {
    return false;
  }
  if (buf[0] != SYNC_FRAME_HEADER0 || buf[1] != SYNC_FRAME_HEADER1) {
    return false;
  }
  if (buf[2] != SYNC_PROTOCOL_VERSION) {
    return false;
  }
  plen = (uint16_t)((uint16_t)buf[4] | ((uint16_t)buf[5] << 8));
  if (len < (size_t)SYNC_FRAME_OVERHEAD + plen) {
    return false;
  }
  calc = sync_crc16(&buf[2], (size_t)4u + plen);
  crc = (uint16_t)((uint16_t)buf[6 + plen] |
                   ((uint16_t)buf[7 + plen] << 8));
  if (crc != calc) {
    return false;
  }
  if (type != NULL) {
    *type = (sync_msg_type_t)buf[3];
  }
  if (payload != NULL) {
    *payload = &buf[6];
  }
  if (payload_len != NULL) {
    *payload_len = plen;
  }
  return true;
}

/* -------------------------------------------------------- status 载荷 */
#define SYNC_STATUS_PAYLOAD_LEN 24u

static void put_u16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)(v & 0xFFu);
  p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void put_u32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFFu);
  p[1] = (uint8_t)((v >> 8) & 0xFFu);
  p[2] = (uint8_t)((v >> 16) & 0xFFu);
  p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static void put_u64(uint8_t *p, uint64_t v) {
  put_u32(p, (uint32_t)(v & 0xFFFFFFFFu));
  put_u32(p + 4, (uint32_t)((v >> 32) & 0xFFFFFFFFu));
}

static uint16_t get_u16(const uint8_t *p) {
  return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t get_u32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

static uint64_t get_u64(const uint8_t *p) {
  return (uint64_t)get_u32(p) | ((uint64_t)get_u32(p + 4) << 32);
}

size_t sync_status_encode(const sync_status_t *status, uint8_t *out, size_t cap) {
  if (status == NULL || out == NULL || cap < SYNC_STATUS_PAYLOAD_LEN) {
    return 0;
  }
  put_u64(out, status->utc_ns);
  put_u32(out + 8, status->pps_seq);
  put_u16(out + 12, status->pps_age_ms);
  put_u32(out + 14, (uint32_t)status->jitter_ns);
  out[18] = status->flags;
  out[19] = status->channel_mask;
  out[20] = status->clock_source;
  out[21] = status->rsv;
  put_u16(out + 22, 0u);
  return SYNC_STATUS_PAYLOAD_LEN;
}

bool sync_status_decode(const uint8_t *in, size_t len, sync_status_t *out) {
  if (in == NULL || out == NULL || len < SYNC_STATUS_PAYLOAD_LEN) {
    return false;
  }
  out->utc_ns = get_u64(in);
  out->pps_seq = get_u32(in + 8);
  out->pps_age_ms = get_u16(in + 12);
  out->jitter_ns = (int32_t)get_u32(in + 14);
  out->flags = in[18];
  out->channel_mask = in[19];
  out->clock_source = in[20];
  out->rsv = in[21];
  return true;
}

/* ------------------------------------------------------------ 触发通道 */
bool sync_channel_cfg_valid(const sync_channel_cfg_t *cfg) {
  if (cfg == NULL) {
    return false;
  }
  if (!cfg->enabled) {
    return true;
  }
  if (cfg->period_us == 0u) {
    return false;
  }
  if (cfg->width_us == 0u || cfg->width_us >= cfg->period_us) {
    return false;
  }
  if (cfg->phase_us >= cfg->period_us) {
    return false;
  }
  return true;
}

uint32_t sync_channel_next_edge_us(const sync_channel_cfg_t *cfg,
                                    uint32_t us_since_pps) {
  uint32_t phase, elapsed, next;
  if (cfg == NULL || !cfg->enabled || cfg->period_us == 0u) {
    return UINT32_MAX;
  }
  phase = cfg->phase_us % cfg->period_us;
  elapsed = us_since_pps % cfg->period_us;
  if (phase >= elapsed) {
    next = phase - elapsed;
  } else {
    next = cfg->period_us - (elapsed - phase);
  }
  return next;
}

bool sync_channel_plan(const sync_channel_cfg_t *cfg, uint32_t timer_hz,
                       sync_timer_plan_t *plan) {
  uint64_t tick_ns, period_ns, width_ns, arr;
  uint32_t psc;
  if (cfg == NULL || plan == NULL || timer_hz == 0u) {
    return false;
  }
  memset(plan, 0, sizeof(*plan));
  if (!cfg->enabled) {
    plan->ok = true; /* 未使能：保持静默 */
    return true;
  }
  if (!sync_channel_cfg_valid(cfg)) {
    return false;
  }
  period_ns = (uint64_t)cfg->period_us * 1000u;
  width_ns = (uint64_t)cfg->width_us * 1000u;
  /* 选最大的 tick 使 ARR 装得下，同时 tick 取整到 1 µs 的整数倍 */
  tick_ns = (period_ns + 65534u) / 65535u;
  tick_ns = ((tick_ns + 999u) / 1000u) * 1000u;
  if (tick_ns * 65535u < period_ns) {
    tick_ns += 1000u; /* 取整到 µs 后可能不够，再加一格 */
  }
  psc = (uint32_t)(tick_ns * (uint64_t)timer_hz / 1000000000ull) - 1u;
  arr = period_ns / tick_ns;
  if (arr < 2u) {
    arr = 2u;
  }
  plan->psc = psc;
  plan->arr = (uint32_t)(arr - 1u);
  plan->ccr = (uint32_t)(width_ns / tick_ns);
  if (plan->ccr == 0u) {
    plan->ccr = 1u;
  }
  if (plan->ccr > plan->arr) {
    plan->ccr = plan->arr;
  }
  plan->tick_ns = (uint32_t)tick_ns;
  plan->ok = true;
  return true;
}

uint32_t sync_channel_oc_compare(const sync_channel_cfg_t *cfg,
                                  uint32_t counter, uint32_t arr) {
  uint32_t delta, mod;
  if (cfg == NULL || !cfg->enabled || cfg->period_us == 0u || arr == 0u) {
    return 0u;
  }
  delta = sync_channel_next_edge_us(cfg, counter);
  if (delta == UINT32_MAX) {
    return 0u;
  }
  mod = arr + 1u;
  return (uint32_t)(((uint64_t)counter + delta) % mod);
}

/* ------------------------------------------------------------ 脉冲指纹 */
const uint8_t sync_fingerprint_default[SYNC_FINGERPRINT_STEPS] = {1, 0, 1, 1, 0, 0};

void sync_fingerprint_widths_us(const uint8_t *code, uint32_t base_us,
                                uint32_t step_us,
                                uint32_t out_us[SYNC_FINGERPRINT_STEPS]) {
  uint32_t i;
  if (code == NULL || out_us == NULL) {
    return;
  }
  for (i = 0; i < SYNC_FINGERPRINT_STEPS; ++i) {
    out_us[i] = base_us + (uint32_t)(code[i] & 0x03u) * step_us;
  }
}

bool sync_fingerprint_matches(const uint32_t *widths_us, const uint8_t *code,
                              uint32_t base_us, uint32_t step_us) {
  uint32_t i, expect;
  if (widths_us == NULL || code == NULL) {
    return false;
  }
  for (i = 0; i < SYNC_FINGERPRINT_STEPS; ++i) {
    expect = base_us + (uint32_t)(code[i] & 0x03u) * step_us;
    if (widths_us[i] != expect) {
      return false;
    }
  }
  return true;
}

/* ------------------------------------------------------------ 时间换算 */
uint64_t sync_gps_ms_to_unix_ms(uint64_t gps_ms, uint32_t leap_seconds) {
  int64_t unix_ms = (int64_t)gps_ms -
                    (int64_t)SYNC_GPS_TO_UNIX_OFFSET_S * 1000LL -
                    (int64_t)leap_seconds * 1000LL;
  return (unix_ms < 0) ? 0u : (uint64_t)unix_ms;
}

uint64_t sync_unix_ms_to_gps_ms(uint64_t unix_ms, uint32_t leap_seconds) {
  return unix_ms + (uint64_t)SYNC_GPS_TO_UNIX_OFFSET_S * 1000ull +
         (uint64_t)leap_seconds * 1000ull;
}

int64_t sync_pps_grid_offset_ns(uint64_t unix_ms, uint64_t measured_unix_ms) {
  int64_t diff_ms = (int64_t)measured_unix_ms - (int64_t)unix_ms;
  int64_t frac = ((diff_ms % 1000LL) + 1000LL) % 1000LL;
  if (frac > 500LL) {
    frac -= 1000LL; /* 归到 [-0.5s, 0.5s] */
  }
  return frac * 1000000LL;
}
