#include "lan9645/lan9645_runtime.h"

static lan9645_core_t *s_lan_core = NULL;
static volatile uint32_t s_irq_edges_pending = 0u;
static lan9645_irq_state_t s_irq_state = {0};

#define LAN9645_IRQ_TIMEOUT_DEFAULT_MS (200u)

static uint32_t lan9645_runtime_irq_lock(void)
{
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  return primask;
}

static void lan9645_runtime_irq_unlock(uint32_t primask)
{
  if (primask == 0u) {
    __enable_irq();
  }
}

void lan9645_runtime_set_core(lan9645_core_t *core)
{
  s_lan_core = core;
}

lan9645_core_t *lan9645_runtime_get_core(void)
{
  return s_lan_core;
}

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

void lan9645_runtime_irq_on_exti(void)
{
  s_irq_edges_pending++;
}

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
