/* test_sync_core.c — 同步板核心逻辑的主机端单元测试
 *
 * 编译: make -C hardware/stm32h7-sync-board/host_test
 * 运行: ./test_sync_core
 */
#include <stdio.h>
#include <string.h>

#include "sync_core.h"

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, fmt, ...)                                                \
  do {                                                                        \
    ++g_checks;                                                               \
    if (!(cond)) {                                                            \
      ++g_failures;                                                           \
      printf("  FAIL %s:%d " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__);     \
    }                                                                         \
  } while (0)

static void test_crc16(void) {
  /* CRC-16/CCITT-FALSE 的标准测试向量 */
  uint16_t c = sync_crc16((const uint8_t *)"123456789", 9);
  printf("crc16(\"123456789\") = 0x%04X\n", c);
  CHECK(c == 0x29B1u, "expected 0x29B1, got 0x%04X", c);
  CHECK(sync_crc16(NULL, 10) == 0xFFFFu, "NULL should return init value");
  CHECK(sync_crc16((const uint8_t *)"", 0) == 0xFFFFu, "empty should be init");
}

static void test_frame_roundtrip(void) {
  uint8_t buf[SYNC_FRAME_MAX];
  uint8_t payload[6] = {1, 2, 3, 4, 5, 6};
  sync_msg_type_t type = SYNC_MSG_STATUS;
  const uint8_t *got = NULL;
  size_t got_len = 0;
  size_t n = sync_frame_build(buf, sizeof(buf), SYNC_MSG_TRIGGER, payload, 6);
  printf("frame len = %zu\n", n);
  CHECK(n == SYNC_FRAME_OVERHEAD + 6u, "unexpected frame length %zu", n);
  CHECK(sync_frame_parse(buf, n, &type, &got, &got_len), "parse should succeed");
  CHECK(type == SYNC_MSG_TRIGGER, "type mismatch");
  CHECK(got_len == 6u && memcmp(got, payload, 6) == 0, "payload mismatch");

  /* 损坏一个字节必须被 CRC 抓到 */
  buf[8] ^= 0x01u;
  CHECK(!sync_frame_parse(buf, n, &type, &got, &got_len), "corrupt frame must fail");

  /* 截断必须失败 */
  CHECK(!sync_frame_parse(buf, n - 1u, &type, &got, &got_len),
        "truncated frame must fail");
  /* 容量不足时 build 返回 0 */
  CHECK(sync_frame_build(buf, 4, SYNC_MSG_TRIGGER, payload, 6) == 0u,
        "build must refuse tiny buffer");
}

static void test_status_codec(void) {
  sync_status_t in;
  sync_status_t out;
  uint8_t buf[32];
  size_t n;
  memset(&in, 0, sizeof(in));
  in.utc_ns = 1735689600123456789ull;
  in.pps_seq = 123456u;
  in.pps_age_ms = 250u;
  in.jitter_ns = -173;
  in.flags = SYNC_FLAG_PPS_LOCKED | SYNC_FLAG_RTC_OK;
  in.channel_mask = 0x0Fu;
  in.clock_source = (uint8_t)SYNC_CLOCK_GNSS;

  n = sync_status_encode(&in, buf, sizeof(buf));
  CHECK(n == 24u, "status payload should be 24 bytes, got %zu", n);
  CHECK(sync_status_decode(buf, n, &out), "decode failed");
  CHECK(out.utc_ns == in.utc_ns, "utc_ns mismatch");
  CHECK(out.pps_seq == in.pps_seq, "pps_seq mismatch");
  CHECK(out.pps_age_ms == in.pps_age_ms, "pps_age_ms mismatch");
  CHECK(out.jitter_ns == in.jitter_ns, "jitter mismatch");
  CHECK(out.flags == in.flags, "flags mismatch");
  CHECK(out.channel_mask == in.channel_mask, "channel_mask mismatch");
  CHECK(out.clock_source == in.clock_source, "clock_source mismatch");
}

static void test_trigger_math(void) {
  sync_channel_cfg_t cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.enabled = true;
  cfg.period_us = 100000u; /* 10 Hz */
  cfg.width_us = 1000u;
  cfg.phase_us = 0u;

  CHECK(sync_channel_cfg_valid(&cfg), "10 Hz config should be valid");

  /* 周期边界上，下一次沿就是现在 */
  CHECK(sync_channel_next_edge_us(&cfg, 0u) == 0u, "edge at t=0");
  CHECK(sync_channel_next_edge_us(&cfg, 1u) == 99999u, "edge just after t=0");
  CHECK(sync_channel_next_edge_us(&cfg, 50000u) == 50000u, "mid period");
  CHECK(sync_channel_next_edge_us(&cfg, 99999u) == 1u, "just before wrap");
  CHECK(sync_channel_next_edge_us(&cfg, 100000u) == 0u, "wrap back to zero");

  /* 带相位 */
  cfg.phase_us = 25000u;
  CHECK(sync_channel_next_edge_us(&cfg, 0u) == 25000u, "phase offset at t=0");
  CHECK(sync_channel_next_edge_us(&cfg, 25000u) == 0u, "phase edge");
  CHECK(sync_channel_next_edge_us(&cfg, 30000u) == 95000u, "after phase edge");

  /* 非法配置 */
  cfg.width_us = cfg.period_us;
  CHECK(!sync_channel_cfg_valid(&cfg), "width >= period must be invalid");
  cfg.width_us = 1000u;
  cfg.phase_us = cfg.period_us;
  CHECK(!sync_channel_cfg_valid(&cfg), "phase >= period must be invalid");
  cfg.phase_us = 0u;
  cfg.period_us = 0u;
  CHECK(!sync_channel_cfg_valid(&cfg), "zero period must be invalid");

  /* 未使能 */
  cfg.enabled = false;
  cfg.period_us = 1000u;
  CHECK(sync_channel_cfg_valid(&cfg), "disabled config is valid");
  CHECK(sync_channel_next_edge_us(&cfg, 0u) == UINT32_MAX,
        "disabled channel has no edge");
}

static void test_timer_plan(void) {
  sync_channel_cfg_t cfg;
  sync_timer_plan_t plan;
  uint32_t timer_hz = 240000000u; /* H7 定时器时钟 240 MHz */

  /* 30 Hz 相机：331/3 us 周期，脉宽 1 ms */
  memset(&cfg, 0, sizeof(cfg));
  cfg.enabled = true;
  cfg.period_us = 33333u;
  cfg.width_us = 1000u;
  CHECK(sync_channel_plan(&cfg, timer_hz, &plan), "plan should succeed");
  printf("30 Hz  -> psc=%u arr=%u ccr=%u tick=%u ns\n", plan.psc, plan.arr,
         plan.ccr, plan.tick_ns);
  CHECK(plan.arr <= 65535u, "ARR must fit 16-bit timer");
  CHECK(plan.tick_ns >= 1000u, "tick should be whole microsecond");
  CHECK(plan.ccr > 0u && plan.ccr < plan.arr, "CCR must be inside the period");
  /* 实际周期 = (arr+1) * tick_ns，应接近目标 */
  {
    uint64_t actual = (uint64_t)(plan.arr + 1u) * plan.tick_ns;
    uint64_t target = (uint64_t)cfg.period_us * 1000ull;
    int64_t err = (int64_t)actual - (int64_t)target;
    printf("         actual period %llu ns, error %lld ns\n",
           (unsigned long long)actual, (long long)err);
    CHECK(err < 1000LL, "period error should be < 1 us, got %lld ns",
          (long long)err);
  }

  /* 1 Hz：需要 1 秒周期，16 位 ARR 装不下 -> 靠预分频 */
  cfg.period_us = 1000000u;
  cfg.width_us = 10000u;
  CHECK(sync_channel_plan(&cfg, timer_hz, &plan), "1 Hz plan should succeed");
  printf("1 Hz   -> psc=%u arr=%u ccr=%u tick=%u ns\n", plan.psc, plan.arr,
         plan.ccr, plan.tick_ns);
  CHECK(plan.arr <= 65535u, "ARR must fit 16-bit timer");
  {
    uint64_t actual = (uint64_t)(plan.arr + 1u) * plan.tick_ns;
    uint64_t target = 1000000000ull;
    printf("         actual period %llu ns\n", (unsigned long long)actual);
    CHECK(actual == target, "1 Hz period should be exact, got %llu",
          (unsigned long long)actual);
  }

  /* 100 Hz IMU */
  cfg.period_us = 10000u;
  cfg.width_us = 500u;
  CHECK(sync_channel_plan(&cfg, timer_hz, &plan), "100 Hz plan should succeed");
  printf("100 Hz -> psc=%u arr=%u ccr=%u tick=%u ns\n", plan.psc, plan.arr,
         plan.ccr, plan.tick_ns);
  CHECK((uint64_t)(plan.arr + 1u) * plan.tick_ns == 10000000ull,
        "100 Hz period should be exact");
}

static void test_fingerprint(void) {
  uint32_t widths[SYNC_FINGERPRINT_STEPS];
  uint32_t widths_bad[SYNC_FINGERPRINT_STEPS];
  int i;
  sync_fingerprint_widths_us(sync_fingerprint_default, 10000u, 20000u, widths);
  printf("fingerprint widths:");
  for (i = 0; i < (int)SYNC_FINGERPRINT_STEPS; ++i) {
    printf(" %u", widths[i]);
  }
  printf("\n");
  CHECK(widths[0] == 30000u, "code 1 -> 30 ms");
  CHECK(widths[1] == 10000u, "code 0 -> 10 ms");
  CHECK(sync_fingerprint_matches(widths, sync_fingerprint_default, 10000u, 20000u),
        "fingerprint should match");
  widths_bad[3] += 1000u;
  CHECK(!sync_fingerprint_matches(widths_bad, sync_fingerprint_default, 10000u,
                                  20000u),
        "modified fingerprint must not match");
}

static void test_time_conversion(void) {
  /* 2025-01-01 00:00:00 UTC = 1735689600 s */
  uint64_t unix_ms = 1735689600000ull;
  uint64_t gps_ms = sync_unix_ms_to_gps_ms(unix_ms, SYNC_LEAP_SECONDS_2017);
  uint64_t back = sync_gps_ms_to_unix_ms(gps_ms, SYNC_LEAP_SECONDS_2017);
  printf("unix_ms=%llu -> gps_ms=%llu -> unix_ms=%llu\n",
         (unsigned long long)unix_ms, (unsigned long long)gps_ms,
         (unsigned long long)back);
  CHECK(back == unix_ms, "round trip must be exact");
  CHECK(gps_ms == unix_ms + 315964800000ull + 37000ull, "gps offset wrong");

  /* 闰秒差异：GPS 与 UTC 相差 37 秒 */
  CHECK(sync_gps_ms_to_unix_ms(gps_ms, 0u) == unix_ms + 37000ull,
        "leap second handling wrong");

  /* PPS 网格偏移 */
  CHECK(sync_pps_grid_offset_ns(1000ull, 1000ull + 20ull) == 20000000LL,
        "grid offset wrong");
  CHECK(sync_pps_grid_offset_ns(1000ull, 1001ull) == 1000000LL,
        "grid offset 1 ms");
  CHECK(sync_pps_grid_offset_ns(1000ull, 999ull) == -1000000LL,
        "grid offset -1 ms");
}

int main(void) {
  printf("== sync_core tests ==\n");
  test_crc16();
  test_frame_roundtrip();
  test_status_codec();
  test_trigger_math();
  test_timer_plan();
  test_fingerprint();
  test_time_conversion();
  printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
