/* sync_host.c — 与主控（Linux/ROS）之间的链路
 *
 * USART1 上跑 sync_core 定义��二进制帧协议：请求/状态/配置/应答。
 * 收中断只做入环缓冲，解析放在主循环，避免在中断里做长操作。
 */
#include "sync_host.h"

#include "main.h"

extern UART_HandleTypeDef huart1;

#define SYNC_RX_BUFFER 256u
#define SYNC_TX_BUFFER 128u

static volatile uint8_t g_rx[SYNC_RX_BUFFER];
static volatile uint16_t g_rx_head;
static volatile uint16_t g_rx_tail;

static uint8_t g_frame[SYNC_FRAME_MAX];
static size_t g_frame_len;

static bool host_read_byte(uint8_t *out) {
  uint16_t tail;
  if (g_rx_head == g_rx_tail) {
    return false;
  }
  tail = g_rx_tail;
  *out = g_rx[tail];
  g_rx_tail = (uint16_t)((tail + 1u) % SYNC_RX_BUFFER);
  return true;
}

static void host_rx_isr(uint8_t byte) {
  uint16_t head = g_rx_head;
  uint16_t next = (uint16_t)((head + 1u) % SYNC_RX_BUFFER);
  if (next == g_rx_tail) {
    return; /* 满，丢字节，主循环会靠 CRC 发现帧损坏 */
  }
  g_rx[head] = byte;
  g_rx_head = next;
}

void sync_host_init(void) {
  g_rx_head = 0u;
  g_rx_tail = 0u;
  g_frame_len = 0u;
  (void)HAL_UART_Receive_IT(&huart1, (uint8_t *)&g_rx[0], 1u);
}

void sync_host_on_rx_isr(void) {
  uint8_t byte = 0u;
  if (HAL_UART_Receive_IT(&huart1, &byte, 1u) == HAL_OK) {
    host_rx_isr(byte);
  }
}

size_t sync_host_send(uint8_t type, const uint8_t *payload, size_t len) {
  uint8_t buf[SYNC_TX_BUFFER];
  size_t n = sync_frame_build(buf, sizeof(buf), (sync_msg_type_t)type, payload,
                              len);
  if (n == 0u) {
    return 0u;
  }
  if (HAL_UART_Transmit(&huart1, buf, (uint16_t)n, 20) != HAL_OK) {
    return 0u;
  }
  return n;
}

/* 主循环里调用：喂收到的字节，返回 true 表示组好一帧并已处理 */
bool sync_host_poll(void) {
  uint8_t byte;
  while (host_read_byte(&byte)) {
    if (g_frame_len < sizeof(g_frame)) {
      g_frame[g_frame_len++] = byte;
    } else {
      g_frame_len = 0u; /* 超长，丢弃 */
    }
    /* 头同步 */
    if (g_frame_len >= 2u &&
        (g_frame[0] != SYNC_FRAME_HEADER0 || g_frame[1] != SYNC_FRAME_HEADER1)) {
      /* 尝试把当前字节当作新帧的第 2 个头 */
      if (byte == SYNC_FRAME_HEADER1) {
        g_frame[0] = SYNC_FRAME_HEADER0;
        g_frame[1] = SYNC_FRAME_HEADER1;
        g_frame_len = 2u;
      } else {
        g_frame_len = 0u;
      }
      continue;
    }
    if (g_frame_len < SYNC_FRAME_OVERHEAD) {
      continue;
    }
    {
      uint16_t plen = (uint16_t)((uint16_t)g_frame[4] | ((uint16_t)g_frame[5] << 8));
      size_t total = (size_t)SYNC_FRAME_OVERHEAD + plen;
      if (g_frame_len < total) {
        continue;
      }
      {
        sync_msg_type_t type = SYNC_MSG_STATUS;
        const uint8_t *payload = NULL;
        size_t payload_len = 0u;
        if (sync_frame_parse(g_frame, g_frame_len, &type, &payload, &payload_len)) {
          (void)sync_board_handle_frame(payload, payload_len);
        }
        g_frame_len = 0u;
        return true;
      }
    }
  }
  return false;
}
