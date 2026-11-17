/* sync_host.h */
#ifndef SYNC_HOST_H
#define SYNC_HOST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sync_core.h"

#ifdef __cplusplus
extern "C" {
#endif

void sync_host_init(void);

/* USART1 接收中断里调用 */
void sync_host_on_rx_isr(void);

/* 主循环里调用；返回 true 表示处理完一帧 */
bool sync_host_poll(void);

/* 发一帧 */
size_t sync_host_send(uint8_t type, const uint8_t *payload, size_t len);

#ifdef __cplusplus
}
#endif
#endif /* SYNC_HOST_H */
