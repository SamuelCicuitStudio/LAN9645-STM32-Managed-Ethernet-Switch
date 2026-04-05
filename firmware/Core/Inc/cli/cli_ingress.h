#ifndef CLI_INGRESS_H
#define CLI_INGRESS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "app_common_types.h"

app_status_t cli_ingress_init(void);
app_status_t cli_ingress_feed_bytes(const uint8_t *data, uint32_t len, app_channel_t source);
app_status_t cli_ingress_feed_byte(uint8_t ch, app_channel_t source);
app_status_t cli_ingress_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* CLI_INGRESS_H */
