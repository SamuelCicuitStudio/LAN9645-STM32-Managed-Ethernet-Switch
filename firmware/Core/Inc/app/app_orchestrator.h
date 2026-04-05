#ifndef APP_ORCHESTRATOR_H
#define APP_ORCHESTRATOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "app_common_types.h"

typedef enum {
  APP_STATE_BOOT = 0,
  APP_STATE_LAN_RESET,
  APP_STATE_LAN_SPI_TEST,
  APP_STATE_LAN_PROFILE_APPLY,
  APP_STATE_NET_READY,
  APP_STATE_RUN,
  APP_STATE_FAULT
} app_state_t;

typedef struct {
  app_state_t state;
  app_state_t prev_state;
  uint32_t last_transition_ms;
  uint32_t fault_code;
} app_orchestrator_snapshot_t;

app_status_t app_orchestrator_init(void);
app_status_t app_orchestrator_set_state(app_state_t state);
app_status_t app_orchestrator_set_fault(uint32_t fault_code);
app_state_t app_orchestrator_get_state(void);
const char *app_orchestrator_state_name(app_state_t state);
app_status_t app_orchestrator_get_snapshot(app_orchestrator_snapshot_t *out);

#ifdef __cplusplus
}
#endif

#endif /* APP_ORCHESTRATOR_H */
