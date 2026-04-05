#include "app/app_orchestrator.h"

/**
 * @file app_orchestrator.c
 * @brief High-level firmware state machine and transition logging.
 */

#include <stdio.h>
#include <string.h>

#include "diag/diag_core.h"
#include "diag/diag_router.h"
#include "lan9645/lan9645_runtime.h"
#include "main.h"
#include "ui/ui_service.h"

/**
 * @brief Internal state wrapper for the application orchestrator.
 */
typedef struct {
  uint8_t initialized;
  app_orchestrator_snapshot_t snap;
} app_orch_state_t;

/**
 * @brief Runtime bookkeeping used by LAN IRQ supervision.
 */
typedef struct {
  uint8_t initialized;
  uint32_t last_edge_count;
  uint32_t last_ana_anaintr;
  uint32_t last_ana_anevents;
  uint32_t last_intr_ident;
  uint32_t last_intr_ident1;
  uint32_t last_dev_pending;
  uint32_t last_report_ms;
  uint32_t report_interval_ms;
} app_orch_lan_irq_state_t;

static app_orch_state_t s_orch = {0};
static app_orch_lan_irq_state_t s_lan_irq = {0};

#define APP_ORCH_LAN_DEV_ID (8u)
#define APP_ORCH_LAN_IRQ_REPORT_INTERVAL_DEFAULT_MS (250u)

/**
 * @brief Maps LAN core IRQ severity to diagnostics severity.
 *
 * @param[in] severity_code LAN core IRQ severity code.
 *
 * @return Diagnostics severity.
 */
static diag_severity_t app_orchestrator_lan_irq_diag_severity(uint8_t severity_code)
{
  switch (severity_code) {
    case LAN9645_CORE_IRQ_SEV_ERR: return DIAG_SEV_ERR;
    case LAN9645_CORE_IRQ_SEV_WARN: return DIAG_SEV_WARN;
    case LAN9645_CORE_IRQ_SEV_INFO:
    default: return DIAG_SEV_INFO;
  }
}

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

/**
 * @brief Initializes LAN IRQ supervision state used by the runtime loop.
 *
 * @param[in] stuck_timeout_ms Timeout used to detect a stuck-high IRQ line.
 * @param[in] cause_report_interval_ms Min interval between repeated cause logs.
 *
 * @return @ref APP_OK on success.
 */
app_status_t app_orchestrator_lan_irq_init(
  uint32_t stuck_timeout_ms,
  uint32_t cause_report_interval_ms
)
{
  memset(&s_lan_irq, 0, sizeof(s_lan_irq));
  s_lan_irq.initialized = 1u;
  s_lan_irq.report_interval_ms =
    (cause_report_interval_ms == 0u) ? APP_ORCH_LAN_IRQ_REPORT_INTERVAL_DEFAULT_MS : cause_report_interval_ms;
  return lan9645_runtime_irq_init(stuck_timeout_ms);
}

/**
 * @brief Runs one LAN IRQ supervision cycle.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] irq_high Sampled IRQ line state (1 = high).
 * @param[in] now_ms Current tick in milliseconds.
 *
 * @return @ref APP_OK on success.
 */
app_status_t app_orchestrator_lan_irq_step(
  lan9645_core_t *core,
  uint8_t irq_high,
  uint32_t now_ms
)
{
  uint8_t irq_stuck_evt = 0u;
  lan9645_irq_state_t irq_state = {0};

  if (core == NULL) {
    return APP_EINVAL;
  }
  if (s_lan_irq.initialized == 0u) {
    (void)app_orchestrator_lan_irq_init(0u, 0u);
  }

  (void)lan9645_runtime_irq_poll(irq_high, now_ms, &irq_stuck_evt);
  (void)lan9645_runtime_irq_get(&irq_state);

  if ((irq_high != 0u) || (irq_state.edge_count != s_lan_irq.last_edge_count)) {
    lan9645_core_irq_regs_t irq_regs = {0};
    app_status_t irq_st = lan9645_core_get_irq_regs(core, &irq_regs);

    if (irq_st == APP_OK) {
      lan9645_core_irq_analysis_t irq_eval = {0};
      uint32_t prev_drop_pending = s_lan_irq.last_ana_anevents & LAN9645_CORE_ANA_EVT_DROP_MASK;
      uint8_t changed = 0u;
      uint8_t periodic = 0u;
      app_status_t eval_st = lan9645_core_eval_irq_actions(&irq_regs, &irq_eval);

      if (eval_st == APP_OK) {
        if ((irq_eval.intr_ident != s_lan_irq.last_intr_ident) ||
            (irq_eval.intr_ident1 != s_lan_irq.last_intr_ident1) ||
            (irq_regs.ana_anaintr != s_lan_irq.last_ana_anaintr) ||
            (irq_eval.ana_anevents != s_lan_irq.last_ana_anevents) ||
            (irq_eval.dev_pending != s_lan_irq.last_dev_pending)) {
          changed = 1u;
        }

        if ((now_ms - s_lan_irq.last_report_ms) >= s_lan_irq.report_interval_ms) {
          periodic = 1u;
        }

        if ((irq_eval.has_cause != 0u) &&
            ((changed != 0u) || (periodic != 0u)) &&
            ((irq_eval.action_mask & LAN9645_CORE_IRQ_ACTION_REPORT_CAUSE) != 0u)) {
          diag_event_t irq_cause_evt;
          irq_cause_evt.id = DIAG_EVT_LAN_IRQ_CAUSE;
          irq_cause_evt.severity = app_orchestrator_lan_irq_diag_severity(irq_eval.severity_code);
          irq_cause_evt.dev = APP_ORCH_LAN_DEV_ID;
          irq_cause_evt.reg = irq_eval.ana_anevents;
          irq_cause_evt.arg0 = irq_eval.intr_ident;
          irq_cause_evt.arg1 = irq_eval.dev_pending;
          (void)diag_core_report(&irq_cause_evt);
          (void)diag_router_write_event(&irq_cause_evt);
          s_lan_irq.last_report_ms = now_ms;
        }

        if (((irq_eval.action_mask & LAN9645_CORE_IRQ_ACTION_REPORT_DROP_ALERT) != 0u) &&
            (irq_eval.drop_events != 0u) &&
            (irq_eval.drop_events != prev_drop_pending)) {
          diag_event_t drop_evt;
          drop_evt.id = DIAG_EVT_LAN_DROP_COUNTER_ALERT;
          drop_evt.severity = DIAG_SEV_WARN;
          drop_evt.dev = APP_ORCH_LAN_DEV_ID;
          drop_evt.reg = irq_eval.ana_anevents;
          drop_evt.arg0 = irq_eval.drop_events;
          drop_evt.arg1 = irq_eval.dev_pending;
          (void)diag_core_report(&drop_evt);
          (void)diag_router_write_event(&drop_evt);
        }

        if ((irq_eval.action_mask & LAN9645_CORE_IRQ_ACTION_ESCALATE_ERROR_LED) != 0u) {
          (void)ui_service_set_error(1u);
        }

        s_lan_irq.last_ana_anaintr = irq_regs.ana_anaintr;
        s_lan_irq.last_ana_anevents = irq_eval.ana_anevents;
        s_lan_irq.last_intr_ident = irq_eval.intr_ident;
        s_lan_irq.last_intr_ident1 = irq_eval.intr_ident1;
        s_lan_irq.last_dev_pending = irq_eval.dev_pending;

        if ((irq_eval.ack_required != 0u) &&
            ((irq_eval.action_mask & LAN9645_CORE_IRQ_ACTION_ACK_ANA_EVENTS) != 0u)) {
          uint32_t remaining = 0u;
          app_status_t ack_st = lan9645_core_ack_irq_checked(core, irq_eval.ana_anevents, &remaining);
          if (ack_st != APP_OK) {
            diag_event_t irq_ack_evt;
            irq_ack_evt.id = DIAG_EVT_LAN_IRQ_ACK_FAIL;
            irq_ack_evt.severity = DIAG_SEV_ERR;
            irq_ack_evt.dev = APP_ORCH_LAN_DEV_ID;
            irq_ack_evt.reg = irq_eval.ana_anevents;
            irq_ack_evt.arg0 = (uint32_t)(int32_t)ack_st;
            irq_ack_evt.arg1 = remaining;
            (void)diag_core_report(&irq_ack_evt);
            (void)diag_router_write_event(&irq_ack_evt);
          }
        }
      }
    } else if ((now_ms - s_lan_irq.last_report_ms) >= s_lan_irq.report_interval_ms) {
      diag_event_t irq_read_evt;
      irq_read_evt.id = DIAG_EVT_LAN_IRQ_CAUSE;
      irq_read_evt.severity = DIAG_SEV_ERR;
      irq_read_evt.dev = APP_ORCH_LAN_DEV_ID;
      irq_read_evt.reg = 0u;
      irq_read_evt.arg0 = (uint32_t)(int32_t)irq_st;
      irq_read_evt.arg1 = 0u;
      (void)diag_core_report(&irq_read_evt);
      (void)diag_router_write_event(&irq_read_evt);
      s_lan_irq.last_report_ms = now_ms;
    }
  }

  s_lan_irq.last_edge_count = irq_state.edge_count;

  if (irq_stuck_evt != 0u) {
    diag_event_t irq_evt;
    irq_evt.id = DIAG_EVT_LAN_IRQ_STUCK;
    irq_evt.severity = DIAG_SEV_WARN;
    irq_evt.dev = APP_ORCH_LAN_DEV_ID;
    irq_evt.reg = 0u;
    irq_evt.arg0 = irq_state.edge_count;
    irq_evt.arg1 = irq_state.stuck_timeout_ms;
    (void)diag_core_report(&irq_evt);
    (void)diag_router_write_event(&irq_evt);
  }

  return APP_OK;
}
