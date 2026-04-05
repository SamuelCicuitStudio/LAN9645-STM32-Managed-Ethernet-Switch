#ifndef LAN9645_RUNTIME_H
#define LAN9645_RUNTIME_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "app_common_types.h"
#include "lan9645/lan9645_core.h"

typedef struct {
  uint8_t initialized;
  uint8_t line_high;
  uint8_t stuck_latched;
  uint32_t edge_count;
  uint32_t last_edge_ms;
  uint32_t high_since_ms;
  uint32_t stuck_timeout_ms;
} lan9645_irq_state_t;

void lan9645_runtime_set_core(lan9645_core_t *core);
lan9645_core_t *lan9645_runtime_get_core(void);
app_status_t lan9645_runtime_irq_init(uint32_t stuck_timeout_ms);
void lan9645_runtime_irq_on_exti(void);
app_status_t lan9645_runtime_irq_poll(uint8_t line_high_sample, uint32_t now_ms, uint8_t *stuck_evt);
app_status_t lan9645_runtime_irq_get(lan9645_irq_state_t *out);
app_status_t lan9645_runtime_irq_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* LAN9645_RUNTIME_H */
