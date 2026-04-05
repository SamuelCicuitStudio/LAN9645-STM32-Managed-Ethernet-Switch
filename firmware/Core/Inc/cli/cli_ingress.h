#ifndef CLI_INGRESS_H
#define CLI_INGRESS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "app_common_types.h"

typedef struct {
  uint32_t bytes_usb;
  uint32_t bytes_uart4;
  uint32_t lines_queued;
  uint32_t lines_executed;
  uint32_t line_overflow_count;
  uint32_t queue_overflow_count;
  uint32_t command_fail_count;
  uint8_t queue_depth;
  uint8_t queue_count;
} cli_ingress_stats_t;

app_status_t cli_ingress_init(void);
app_status_t cli_ingress_feed_bytes(const uint8_t *data, uint32_t len, app_channel_t source);
app_status_t cli_ingress_feed_byte(uint8_t ch, app_channel_t source);
app_status_t cli_ingress_poll(void);
app_status_t cli_ingress_get_stats(cli_ingress_stats_t *out);

#ifdef __cplusplus
}
#endif

#endif /* CLI_INGRESS_H */
