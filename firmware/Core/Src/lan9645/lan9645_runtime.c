#include "lan9645/lan9645_runtime.h"

/**
 * @file lan9645_runtime.c
 * @brief Shared IRQ-line runtime tracking for the LAN9645 integration.
 */

static lan9645_core_t *s_lan_core = NULL;
static lan9645_feature_matrix_t *s_feature_matrix = NULL;
static volatile uint32_t s_irq_edges_pending = 0u;
static lan9645_irq_state_t s_irq_state = {0};

#define LAN9645_IRQ_TIMEOUT_DEFAULT_MS (200u)

/**
 * @brief Enters a short critical section used by the IRQ monitor.
 *
 * @return Previous PRIMASK state.
 */
static uint32_t lan9645_runtime_irq_lock(void)
{
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  return primask;
}

/**
 * @brief Restores the interrupt state saved by @ref lan9645_runtime_irq_lock.
 *
 * @param[in] primask Saved PRIMASK value.
 */
static void lan9645_runtime_irq_unlock(uint32_t primask)
{
  if (primask == 0u) {
    __enable_irq();
  }
}

/**
 * @brief Stores the global LAN9645 core pointer used by runtime helpers.
 *
 * @param[in] core Core pointer to store.
 */
void lan9645_runtime_set_core(lan9645_core_t *core)
{
  s_lan_core = core;
}

/**
 * @brief Returns the global LAN9645 core pointer.
 *
 * @return Current core pointer, or NULL when not installed.
 */
lan9645_core_t *lan9645_runtime_get_core(void)
{
  return s_lan_core;
}

/**
 * @brief Stores the global feature-matrix pointer used by runtime helpers.
 *
 * @param[in] matrix Feature matrix pointer to store.
 */
void lan9645_runtime_set_feature_matrix(lan9645_feature_matrix_t *matrix)
{
  s_feature_matrix = matrix;
}

/**
 * @brief Returns the global feature-matrix pointer.
 *
 * @return Current feature matrix pointer, or NULL when not installed.
 */
lan9645_feature_matrix_t *lan9645_runtime_get_feature_matrix(void)
{
  return s_feature_matrix;
}

/**
 * @brief Initializes IRQ-line monitoring state.
 *
 * @param[in] stuck_timeout_ms Timeout used to flag a stuck IRQ line.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_runtime_irq_init(uint32_t stuck_timeout_ms)
{
  uint32_t lock = lan9645_runtime_irq_lock();

  s_irq_edges_pending = 0u;
  s_irq_state.initialized = 1u;
  s_irq_state.line_high = 0u;
  s_irq_state.stuck_latched = 0u;
  s_irq_state.edge_count = 0u;
  s_irq_state.last_edge_ms = 0u;
  s_irq_state.high_since_ms = 0u;
  s_irq_state.stuck_timeout_ms = (stuck_timeout_ms == 0u) ? LAN9645_IRQ_TIMEOUT_DEFAULT_MS : stuck_timeout_ms;

  lan9645_runtime_irq_unlock(lock);
  return APP_OK;
}

/**
 * @brief Records one EXTI edge on the LAN IRQ line.
 */
void lan9645_runtime_irq_on_exti(void)
{
  s_irq_edges_pending++;
}

/**
 * @brief Updates IRQ state from a sampled GPIO level and pending EXTI edges.
 *
 * @param[in] line_high_sample Current sampled IRQ line level.
 * @param[in] now_ms Current tick in milliseconds.
 * @param[out] stuck_evt Optional one-shot flag set when a stuck condition appears.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_runtime_irq_poll(uint8_t line_high_sample, uint32_t now_ms, uint8_t *stuck_evt)
{
  uint32_t pending = 0u;
  uint32_t lock = 0u;

  if (stuck_evt != NULL) {
    *stuck_evt = 0u;
  }

  if (s_irq_state.initialized == 0u) {
    (void)lan9645_runtime_irq_init(0u);
  }

  lock = lan9645_runtime_irq_lock();
  pending = s_irq_edges_pending;
  s_irq_edges_pending = 0u;

  if (pending != 0u) {
    s_irq_state.edge_count += pending;
    s_irq_state.last_edge_ms = now_ms;
  }

  s_irq_state.line_high = (line_high_sample != 0u) ? 1u : 0u;

  if (s_irq_state.line_high != 0u) {
    if (s_irq_state.high_since_ms == 0u) {
      s_irq_state.high_since_ms = now_ms;
    }

    if ((s_irq_state.stuck_latched == 0u) &&
        ((now_ms - s_irq_state.high_since_ms) >= s_irq_state.stuck_timeout_ms)) {
      s_irq_state.stuck_latched = 1u;
      if (stuck_evt != NULL) {
        *stuck_evt = 1u;
      }
    }
  } else {
    s_irq_state.high_since_ms = 0u;
    s_irq_state.stuck_latched = 0u;
  }

  lan9645_runtime_irq_unlock(lock);
  return APP_OK;
}

/**
 * @brief Returns a snapshot of the current IRQ-line monitor state.
 *
 * @param[out] out Destination snapshot.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_runtime_irq_get(lan9645_irq_state_t *out)
{
  uint32_t lock = 0u;

  if (out == NULL) {
    return APP_EINVAL;
  }
  if (s_irq_state.initialized == 0u) {
    (void)lan9645_runtime_irq_init(0u);
  }

  lock = lan9645_runtime_irq_lock();
  *out = s_irq_state;
  lan9645_runtime_irq_unlock(lock);
  return APP_OK;
}

/**
 * @brief Clears IRQ counters and re-arms stuck-line monitoring.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_runtime_irq_clear(void)
{
  uint32_t timeout = LAN9645_IRQ_TIMEOUT_DEFAULT_MS;
  uint32_t lock = lan9645_runtime_irq_lock();

  if (s_irq_state.stuck_timeout_ms != 0u) {
    timeout = s_irq_state.stuck_timeout_ms;
  }

  s_irq_edges_pending = 0u;
  s_irq_state.initialized = 1u;
  s_irq_state.line_high = 0u;
  s_irq_state.stuck_latched = 0u;
  s_irq_state.edge_count = 0u;
  s_irq_state.last_edge_ms = 0u;
  s_irq_state.high_since_ms = 0u;
  s_irq_state.stuck_timeout_ms = timeout;

  lan9645_runtime_irq_unlock(lock);
  return APP_OK;
}
