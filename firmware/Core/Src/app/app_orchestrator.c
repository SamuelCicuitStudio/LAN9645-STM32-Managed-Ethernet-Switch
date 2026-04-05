#include "app/app_orchestrator.h"

/**
 * @file app_orchestrator.c
 * @brief High-level firmware state machine and transition logging.
 */

#include <stdio.h>
#include <string.h>

#include "diag/diag_router.h"
#include "main.h"

/**
 * @brief Internal state wrapper for the application orchestrator.
 */
typedef struct {
  uint8_t initialized;
  app_orchestrator_snapshot_t snap;
} app_orch_state_t;

static app_orch_state_t s_orch = {0};

/**
 * @brief Converts an application state into a readable label.
 *
 * @param[in] state State to convert.
 *
 * @return Constant string representation.
 */
const char *app_orchestrator_state_name(app_state_t state)
{
  switch (state) {
    case APP_STATE_BOOT: return "BOOT";
    case APP_STATE_LAN_RESET: return "LAN_RESET";
    case APP_STATE_LAN_SPI_TEST: return "LAN_SPI_TEST";
    case APP_STATE_LAN_PROFILE_APPLY: return "LAN_PROFILE_APPLY";
    case APP_STATE_NET_READY: return "NET_READY";
    case APP_STATE_RUN: return "RUN";
    case APP_STATE_FAULT: return "FAULT";
    default: return "UNKNOWN";
  }
}

/**
 * @brief Initializes the application state machine to the boot state.
 *
 * @return @ref APP_OK on success.
 */
app_status_t app_orchestrator_init(void)
{
  memset(&s_orch, 0, sizeof(s_orch));
  s_orch.initialized = 1u;
  s_orch.snap.state = APP_STATE_BOOT;
  s_orch.snap.prev_state = APP_STATE_BOOT;
  s_orch.snap.last_transition_ms = HAL_GetTick();
  s_orch.snap.fault_code = 0u;
  return APP_OK;
}

/**
 * @brief Performs a state transition and logs it on the diagnostics route.
 *
 * @param[in] state New application state.
 *
 * @return @ref APP_OK on success.
 */
app_status_t app_orchestrator_set_state(app_state_t state)
{
  char line[96];

  if (s_orch.initialized == 0u) {
    (void)app_orchestrator_init();
  }

  if (s_orch.snap.state == state) {
    return APP_OK;
  }

  s_orch.snap.prev_state = s_orch.snap.state;
  s_orch.snap.state = state;
  s_orch.snap.last_transition_ms = HAL_GetTick();
  if (state != APP_STATE_FAULT) {
    s_orch.snap.fault_code = 0u;
  }

  (void)snprintf(
    line,
    sizeof(line),
    "state %s -> %s\r\n",
    app_orchestrator_state_name(s_orch.snap.prev_state),
    app_orchestrator_state_name(s_orch.snap.state)
  );
  (void)diag_router_write_text(line);

  return APP_OK;
}

/**
 * @brief Forces the application into fault state and latches a fault code.
 *
 * @param[in] fault_code Fault reason to store.
 *
 * @return @ref APP_OK on success.
 */
app_status_t app_orchestrator_set_fault(uint32_t fault_code)
{
  app_status_t st = app_orchestrator_set_state(APP_STATE_FAULT);
  if (st != APP_OK) {
    return st;
  }
  s_orch.snap.fault_code = fault_code;
  return APP_OK;
}

/**
 * @brief Returns the current orchestrator state.
 *
 * @return Current application state.
 */
app_state_t app_orchestrator_get_state(void)
{
  if (s_orch.initialized == 0u) {
    (void)app_orchestrator_init();
  }
  return s_orch.snap.state;
}

/**
 * @brief Returns a snapshot of the orchestrator state machine.
 *
 * @param[out] out Destination snapshot.
 *
 * @return @ref APP_OK on success.
 */
app_status_t app_orchestrator_get_snapshot(app_orchestrator_snapshot_t *out)
{
  if (out == NULL) {
    return APP_EINVAL;
  }
  if (s_orch.initialized == 0u) {
    (void)app_orchestrator_init();
  }

  *out = s_orch.snap;
  return APP_OK;
}
