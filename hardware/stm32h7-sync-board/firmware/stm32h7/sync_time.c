/* sync_time.c — UTC 时间源管理
 *
 * 两个来源：
 *   1) GNSS/RTK 的 NMEA（USART2，推荐）—— 直接给出 UTC 与定位状态
 *   2) DS3231 RTC（I2C1，备份）—— GNSS 不可用时维持时间
 * GNSS 在线时，PPS 提供秒边界，NMEA 提供秒内的绝对时刻；
 * 二者结合才能把 PPS 抖动换算成 UTC 纳秒时间戳。
 */
#include "sync_time.h"

#include "main.h"
#include <string.h>

extern I2C_HandleTypeDef hi2c1;

#define SYNC_DS3231_ADDR 0x68u
#define SYNC_DS3231_REG_TIME 0x00u

static sync_time_state_t g_time;
static char g_nmea[128];
static size_t g_nmea_len;

/* ---------------------------------------------------------------- DS3231 */
static uint8_t bcd2bin(uint8_t v) {
  return (uint8_t)(((v >> 4) * 10u) + (v & 0x0Fu));
}

/* Howard Hinnant 的 days_from_civil，避免依赖 libc 的时区处理 */
static int64_t days_from_civil(int64_t y, unsigned m, unsigned d) {
  int64_t era;
  unsigned yoe, doy, doe;
  y -= (m <= 2u) ? 1 : 0;
  era = ((y >= 0 ? y : y - 399) / 400);
  yoe = (unsigned)(y - era * 400);
  doy = (153u * (m + (m > 2u ? (unsigned)-3 : 9u)) + 2u) / 5u + d - 1u;
  doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
  return era * 146097 + (int64_t)doe - 719468;
}

bool sync_time_read_rtc(uint64_t *unix_ms) {
  uint8_t raw[7];
  uint8_t sec, min, hour, day, mon, year;
  uint64_t days;

  if (unix_ms == NULL) {
    return false;
  }
  if (HAL_I2C_Mem_Read(&hi2c1, SYNC_DS3231_ADDR, SYNC_DS3231_REG_TIME,
                      I2C_MEMADD_SIZE_8BIT, raw, sizeof(raw), 50) != HAL_OK) {
    g_time.rtc_valid = false;
    return false;
  }
  sec = bcd2bin((uint8_t)(raw[0] & 0x7Fu));
  min = bcd2bin(raw[1]);
  hour = bcd2bin((uint8_t)(raw[2] & 0x3Fu));
  day = bcd2bin(raw[4]);
  mon = bcd2bin((uint8_t)(raw[5] & 0x7Fu));
  year = bcd2bin(raw[6]);

  days = (uint64_t)days_from_civil(2000 + (int64_t)year, mon, day);
  *unix_ms = (days * 86400ull + (uint64_t)hour * 3600ull +
              (uint64_t)min * 60ull + (uint64_t)sec) *
             1000ull;
  g_time.rtc_valid = true;
  return true;
}

/* ---------------------------------------------------------------- NMEA */
/* 手写解析，避免在 MCU 上引入 stdio：$GNRMC,hhmmss.sss,A,... */
static bool parse_nmea_time(const char *line, uint8_t *hh, uint8_t *mm,
                            double *ss, char *status) {
  const char *p;
  size_t i = 0;

  if (strncmp(line, "$GNRMC", 6) != 0 && strncmp(line, "$GPRMC", 6) != 0) {
    return false;
  }
  p = strchr(line + 6, ',');
  if (p == NULL) {
    return false;
  }
  ++p;
  if (strlen(p) < 10u) {
    return false;
  }
  if (p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9' ||
      p[2] < '0' || p[2] > '9' || p[3] < '0' || p[3] > '9' ||
      p[4] < '0' || p[4] > '9' || p[5] < '0' || p[5] > '9') {
    return false;
  }
  *hh = (uint8_t)((p[0] - '0') * 10 + (p[1] - '0'));
  *mm = (uint8_t)((p[2] - '0') * 10 + (p[3] - '0'));
  {
    uint32_t whole = (uint32_t)((p[4] - '0') * 10 + (p[5] - '0'));
    double frac = 0.0;
    if (p[6] == '.') {
      double scale = 0.1;
      ++p;
      for (i = 0; p[i] >= '0' && p[i] <= '9'; ++i) {
        frac += (double)(p[i] - '0') * scale;
        scale *= 0.1;
      }
    }
    *ss = (double)whole + frac;
  }
  p = strchr(p, ',');
  if (p == NULL) {
    return false;
  }
  *status = p[1];
  return true;
}

bool sync_time_feed_nmea(const char *line, size_t len) {
  uint8_t hh = 0u, mm = 0u;
  double ss = 0.0;
  char status = 'V';

  (void)len;
  if (line == NULL) {
    return false;
  }
  if (!parse_nmea_time(line, &hh, &mm, &ss, &status)) {
    return false;
  }
  g_time.nmea_valid = (status == 'A');
  g_time.nmea_h = hh;
  g_time.nmea_m = mm;
  g_time.nmea_s = ss;
  return true;
}

void sync_time_nmea_push(uint8_t byte) {
  if (byte == '\r' || byte == '\n') {
    if (g_nmea_len > 0u) {
      g_nmea[g_nmea_len] = '\0';
      (void)sync_time_feed_nmea(g_nmea, g_nmea_len);
      g_nmea_len = 0u;
    }
    return;
  }
  if (g_nmea_len < (sizeof(g_nmea) - 1u)) {
    g_nmea[g_nmea_len++] = (char)byte;
  } else {
    g_nmea_len = 0u;
  }
}

bool sync_time_gnss_utc_ms(uint64_t *unix_ms) {
  uint64_t base;
  if (unix_ms == NULL) {
    return false;
  }
  if (!g_time.nmea_valid) {
    return false;
  }
  if (!sync_time_read_rtc(&base)) {
    return false;
  }
  {
    uint64_t midnight_ms = base - ((base / 1000ull) % 86400ull) * 1000ull;
    *unix_ms = midnight_ms + (uint64_t)g_time.nmea_h * 3600000ull +
               (uint64_t)g_time.nmea_m * 60000ull +
               (uint64_t)(g_time.nmea_s * 1000.0);
  }
  return true;
}

const sync_time_state_t *sync_time_state(void) { return &g_time; }
