#ifndef DIAG_ROUTER_H
#define DIAG_ROUTER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "app_common_types.h"
#include "diag/diag_core.h"

typedef struct {
  app_owner_t owner;
  uint8_t usb_ready;
  uint8_t uart_ready;
  uint8_t mirror_when_none;
  uint8_t bridge_usb_uart_on;
} diag_router_state_t;

app_status_t diag_router_init(void);
app_status_t diag_router_set_owner(app_owner_t owner);
app_owner_t diag_router_get_owner(void);
app_status_t diag_router_set_usb_ready(uint8_t ready);
app_status_t diag_router_set_uart_ready(uint8_t ready);
app_status_t diag_router_write_text(const char *line);
app_status_t diag_router_write_event(const diag_event_t *evt);
app_status_t diag_router_set_bridge(uint8_t on);
app_status_t diag_router_get_state(diag_router_state_t *out);
app_status_t diag_router_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* DIAG_ROUTER_H */
