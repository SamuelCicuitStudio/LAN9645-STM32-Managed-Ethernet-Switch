#ifndef APP_ORCHESTRATOR_H
#define APP_ORCHESTRATOR_H

/**
 * @file app_orchestrator.h
 * @brief High-level boot and runtime state machine for the firmware.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "app_common_types.h"
#include "lan9645/lan9645_core.h"

/**
 * @brief Main firmware phases used during bring-up and steady-state operation.
 */
typedef enum {
  APP_STATE_BOOT = 0,
  APP_STATE_LAN_RESET,
  APP_STATE_LAN_SPI_TEST,
  APP_STATE_LAN_PROFILE_APPLY,
  APP_STATE_NET_READY,
  APP_STATE_RUN,
  APP_STATE_FAULT
} app_state_t;

/**
 * @brief Snapshot of the orchestrator state machine.
 */
typedef struct {
  app_state_t state;             /**< Current orchestrator state. */
  app_state_t prev_state;        /**< Previous state before the last transition. */
  uint32_t last_transition_ms;   /**< Tick count captured at the last transition. */
  uint32_t fault_code;           /**< Latched fault code when state is @ref APP_STATE_FAULT. */
} app_orchestrator_snapshot_t;

/**
 * @brief Initializes the orchestrator state machine.
 *
 * @return @ref APP_OK on success.
 */
app_status_t app_orchestrator_init(void);

/**
 * @brief Moves the application into a new orchestrator state.
 *
 * @param[in] state Next application state.
 *
 * @return @ref APP_OK on success.
 */
app_status_t app_orchestrator_set_state(app_state_t state);

/**
 * @brief Latches a fault code and transitions the application into fault state.
 *
 * @param[in] fault_code Firmware-defined fault reason.
 *
 * @return @ref APP_OK on success.
 */
app_status_t app_orchestrator_set_fault(uint32_t fault_code);

/**
 * @brief Returns the current orchestrator state.
 *
 * @return Current application state.
 */
app_state_t app_orchestrator_get_state(void);

/**
 * @brief Converts an application state into a printable name.
 *
 * @param[in] state State value to convert.
 *
 * @return Pointer to a constant string describing the state.
 */
const char *app_orchestrator_state_name(app_state_t state);

/**
 * @brief Returns a full snapshot of the orchestrator state machine.
 *
 * @param[out] out Destination structure for the snapshot.
 *
 * @return @ref APP_OK on success.
 */
app_status_t app_orchestrator_get_snapshot(app_orchestrator_snapshot_t *out);

/**
 * @brief Initializes LAN IRQ supervision used by the main runtime loop.
 *
 * This helper owns the software state used to detect changes in LAN9645 IRQ
 * causes, periodic reporting, and stuck IRQ line conditions.
 *
 * @param[in] stuck_timeout_ms Timeout used to classify IRQ line stuck-high.
 * @param[in] cause_report_interval_ms Minimum interval between repeated
 *            identical IRQ-cause reports.
 *
 * @return @ref APP_OK on success.
 */
app_status_t app_orchestrator_lan_irq_init(
  uint32_t stuck_timeout_ms,
  uint32_t cause_report_interval_ms
);

/**
 * @brief Performs one LAN IRQ supervision step.
 *
 * The function polls IRQ line runtime state, reads/evaluates LAN9645 IRQ
 * registers when needed, reports diagnostics events, acknowledges ANA events,
 * and raises the UI error indicator when severe events are detected.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] irq_high Sampled logic level of the IRQ line (1 = high).
 * @param[in] now_ms Current system tick in milliseconds.
 *
 * @return @ref APP_OK on success.
 */
app_status_t app_orchestrator_lan_irq_step(
  lan9645_core_t *core,
  uint8_t irq_high,
  uint32_t now_ms
);

#ifdef __cplusplus
}
#endif

#endif /* APP_ORCHESTRATOR_H */
