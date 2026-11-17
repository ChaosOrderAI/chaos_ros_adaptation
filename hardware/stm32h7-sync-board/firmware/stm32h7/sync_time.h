/* sync_time.h */
#ifndef SYNC_TIME_H
#define SYNC_TIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  bool rtc_valid;
  bool nmea_valid;
  uint8_t nmea_h;
  uint8_t nmea_m;
  double nmea_s;
} sync_time_state_t;

/* 从 DS3231 读 UTC（Unix 毫秒） */
bool sync_time_read_rtc(uint64_t *unix_ms);

/* 喂一整行 NMEA（不含换行），解析 RMC 的 UTC 与定位状态 */
bool sync_time_feed_nmea(const char *line, size_t len);

/* 逐字节喂 NMEA，遇到换行结算一行 */
void sync_time_nmea_push(uint8_t byte);

/* GNSS 有效时给出 UTC（Unix 毫秒）：RTC 日期 + NMEA 时分秒 */
bool sync_time_gnss_utc_ms(uint64_t *unix_ms);

const sync_time_state_t *sync_time_state(void);

#ifdef __cplusplus
}
#endif
#endif /* SYNC_TIME_H */
