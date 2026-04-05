#ifndef LAN9645_REG_H
#define LAN9645_REG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "app_common_types.h"
#include "lan9645/lan9645_regs.h"
#include "lan9645/lan9645_spi.h"

typedef struct {
  lan9645_spi_t *spi;
  uint32_t target_base[LAN9645_TARGET_COUNT];
  uint8_t target_base_valid[LAN9645_TARGET_COUNT];
  uint8_t initialized;
} lan9645_reg_t;

app_status_t lan9645_reg_init(lan9645_reg_t *ctx, lan9645_spi_t *spi);
app_status_t lan9645_reg_set_target_base(
  lan9645_reg_t *ctx,
  lan9645_target_t target,
  uint8_t target_instance,
  uint32_t base_addr
);
app_status_t lan9645_reg_clear_target_base(
  lan9645_reg_t *ctx,
  lan9645_target_t target,
  uint8_t target_instance
);
app_status_t lan9645_reg_get_target_base(
  lan9645_reg_t *ctx,
  lan9645_target_t target,
  uint8_t target_instance,
  uint32_t *base_addr
);
app_status_t lan9645_reg_read32(lan9645_reg_t *ctx, uint32_t reg_addr, uint32_t *value);
app_status_t lan9645_reg_write32(lan9645_reg_t *ctx, uint32_t reg_addr, uint32_t value);
app_status_t lan9645_reg_rmw32(lan9645_reg_t *ctx, uint32_t reg_addr, uint32_t mask, uint32_t value);
app_status_t lan9645_reg_wait_field(
  lan9645_reg_t *ctx,
  uint32_t reg_addr,
  uint32_t mask,
  uint32_t expected,
  uint32_t timeout_ms,
  uint32_t *last_read
);
app_status_t lan9645_reg_read32_desc(
  lan9645_reg_t *ctx,
  const lan9645_reg_desc_t *desc,
  uint32_t target_instance,
  uint32_t group_instance,
  uint32_t reg_instance,
  uint32_t *value
);
app_status_t lan9645_reg_write32_desc(
  lan9645_reg_t *ctx,
  const lan9645_reg_desc_t *desc,
  uint32_t target_instance,
  uint32_t group_instance,
  uint32_t reg_instance,
  uint32_t value
);
app_status_t lan9645_reg_rmw32_desc(
  lan9645_reg_t *ctx,
  const lan9645_reg_desc_t *desc,
  uint32_t target_instance,
  uint32_t group_instance,
  uint32_t reg_instance,
  uint32_t mask,
  uint32_t value
);
app_status_t lan9645_reg_wait32_desc(
  lan9645_reg_t *ctx,
  const lan9645_reg_desc_t *desc,
  uint32_t target_instance,
  uint32_t group_instance,
  uint32_t reg_instance,
  uint32_t mask,
  uint32_t expected,
  uint32_t timeout_ms,
  uint32_t *last_read
);

#ifdef __cplusplus
}
#endif

#endif /* LAN9645_REG_H */
