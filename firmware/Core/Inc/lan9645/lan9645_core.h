#ifndef LAN9645_CORE_H
#define LAN9645_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "app_common_types.h"
#include "lan9645/lan9645_reg.h"
#include "main.h"

#define LAN9645_CORE_SOFT_RESET_TIMEOUT_MS (200u)

typedef struct {
  lan9645_reg_t *reg;
  GPIO_TypeDef *rst_port;
  uint16_t rst_pin;
  uint8_t initialized;
} lan9645_core_t;

app_status_t lan9645_core_init(
  lan9645_core_t *ctx,
  lan9645_reg_t *reg,
  GPIO_TypeDef *rst_port,
  uint16_t rst_pin
);
app_status_t lan9645_core_hw_reset(lan9645_core_t *ctx, uint32_t assert_ms, uint32_t release_ms);
app_status_t lan9645_core_soft_reset(lan9645_core_t *ctx, uint32_t timeout_ms);
app_status_t lan9645_core_read_feat_disable(lan9645_core_t *ctx, uint32_t *feat_disable);
app_status_t lan9645_core_set_port_enable(lan9645_core_t *ctx, uint8_t port, uint8_t enable);
app_status_t lan9645_core_get_rgmii_link(
  lan9645_core_t *ctx,
  uint8_t rgmii_idx,
  uint8_t *link_up,
  uint8_t *duplex,
  uint8_t *speed_code
);

#ifdef __cplusplus
}
#endif

#endif /* LAN9645_CORE_H */
