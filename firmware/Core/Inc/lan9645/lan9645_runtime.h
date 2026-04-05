#ifndef LAN9645_RUNTIME_H
#define LAN9645_RUNTIME_H

/**
 * @file lan9645_runtime.h
 * @brief Shared runtime state for LAN9645 IRQ monitoring and polling helpers.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "app_common_types.h"
#include "lan9645/lan9645_core.h"

/**
 * @brief Snapshot of the external LAN IRQ line monitor.
 */
typedef struct {
  uint8_t initialized;      /**< Set when IRQ monitoring has been initialized. */
  uint8_t line_high;        /**< Last sampled logic level on the IRQ line. */
  uint8_t stuck_latched;    /**< Set once the line has remained high beyond the timeout. */
  uint32_t edge_count;      /**< Number of EXTI edges observed on the IRQ input. */
  uint32_t last_edge_ms;    /**< Timestamp of the most recent IRQ edge. */
  uint32_t high_since_ms;   /**< Timestamp when the line most recently went high. */
  uint32_t stuck_timeout_ms;/**< Timeout used to classify the line as stuck. */
} lan9645_irq_state_t;

/**
 * @brief Installs the global LAN9645 core pointer used by runtime helpers.
 *
 * @param[in] core Core instance to expose.
 */
void lan9645_runtime_set_core(lan9645_core_t *core);

/**
 * @brief Returns the currently installed global LAN9645 core pointer.
 *
 * @return Pointer previously provided to @ref lan9645_runtime_set_core.
 */
lan9645_core_t *lan9645_runtime_get_core(void);

/**
 * @brief Initializes IRQ runtime tracking.
 *
 * @param[in] stuck_timeout_ms High-level timeout used to flag a stuck IRQ line.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_runtime_irq_init(uint32_t stuck_timeout_ms);

/**
 * @brief Records one IRQ edge from the EXTI callback context.
 */
void lan9645_runtime_irq_on_exti(void);

/**
 * @brief Polls the IRQ line state and updates stuck-line detection.
 *
 * @param[in] line_high_sample Current sampled logic level of the IRQ pin.
 * @param[in] now_ms Current millisecond tick.
 * @param[out] stuck_evt Optional one-shot flag set when a stuck event is newly detected.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_runtime_irq_poll(uint8_t line_high_sample, uint32_t now_ms, uint8_t *stuck_evt);

/**
 * @brief Returns a snapshot of the IRQ runtime state.
 *
 * @param[out] out Destination structure for the IRQ state.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_runtime_irq_get(lan9645_irq_state_t *out);

/**
 * @brief Clears the latched IRQ runtime counters and stuck-line state.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_runtime_irq_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* LAN9645_RUNTIME_H */
