#include "lan9645/lan9645_core.h"

#include <stddef.h>
#include <string.h>

static app_status_t lan9645_core_check_ctx(const lan9645_core_t *ctx)
{
  if (ctx == NULL) {
    return APP_EINVAL;
  }
  if ((ctx->initialized == 0u) || (ctx->reg == NULL)) {
    return APP_ESTATE;
  }
  return APP_OK;
}

app_status_t lan9645_core_init(
  lan9645_core_t *ctx,
  lan9645_reg_t *reg,
  GPIO_TypeDef *rst_port,
  uint16_t rst_pin
)
{
  if ((ctx == NULL) || (reg == NULL) || (rst_port == NULL) || (rst_pin == 0u)) {
    return APP_EINVAL;
  }

  memset(ctx, 0, sizeof(*ctx));
  ctx->reg = reg;
  ctx->rst_port = rst_port;
  ctx->rst_pin = rst_pin;
  ctx->initialized = 1u;
  return APP_OK;
}

app_status_t lan9645_core_hw_reset(lan9645_core_t *ctx, uint32_t assert_ms, uint32_t release_ms)
{
  app_status_t st = lan9645_core_check_ctx(ctx);
  if (st != APP_OK) {
    return st;
  }

  HAL_GPIO_WritePin(ctx->rst_port, ctx->rst_pin, GPIO_PIN_RESET);
  HAL_Delay(assert_ms);
  HAL_GPIO_WritePin(ctx->rst_port, ctx->rst_pin, GPIO_PIN_SET);
  HAL_Delay(release_ms);
  return APP_OK;
}

app_status_t lan9645_core_soft_reset(lan9645_core_t *ctx, uint32_t timeout_ms)
{
  uint32_t local_timeout = timeout_ms;
  app_status_t st = lan9645_core_check_ctx(ctx);
  if (st != APP_OK) {
    return st;
  }

  if (local_timeout == 0u) {
    local_timeout = LAN9645_CORE_SOFT_RESET_TIMEOUT_MS;
  }

  st = lan9645_reg_rmw32_desc(
    ctx->reg,
    &LAN9645_REG_SYS_RESET_CFG,
    0u,
    0u,
    0u,
    LAN9645_SYS_RESET_CFG_CORE_ENA,
    LAN9645_SYS_RESET_CFG_CORE_ENA_SET(0u)
  );
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_reg_rmw32_desc(
    ctx->reg,
    &LAN9645_REG_SYS_RAM_INIT,
    0u,
    0u,
    0u,
    LAN9645_SYS_RAM_INIT_RAM_INIT,
    LAN9645_SYS_RAM_INIT_RAM_INIT_SET(1u)
  );
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_reg_wait32_desc(
    ctx->reg,
    &LAN9645_REG_SYS_RAM_INIT,
    0u,
    0u,
    0u,
    LAN9645_SYS_RAM_INIT_RAM_INIT,
    LAN9645_SYS_RAM_INIT_RAM_INIT_SET(0u),
    local_timeout,
    NULL
  );
  if (st != APP_OK) {
    return st;
  }

  return lan9645_reg_rmw32_desc(
    ctx->reg,
    &LAN9645_REG_SYS_RESET_CFG,
    0u,
    0u,
    0u,
    LAN9645_SYS_RESET_CFG_CORE_ENA,
    LAN9645_SYS_RESET_CFG_CORE_ENA_SET(1u)
  );
}

app_status_t lan9645_core_read_feat_disable(lan9645_core_t *ctx, uint32_t *feat_disable)
{
  app_status_t st = lan9645_core_check_ctx(ctx);
  if (st != APP_OK) {
    return st;
  }
  if (feat_disable == NULL) {
    return APP_EINVAL;
  }

  return lan9645_reg_read32_desc(
    ctx->reg,
    &LAN9645_REG_GCB_FEAT_DISABLE,
    0u,
    0u,
    0u,
    feat_disable
  );
}

app_status_t lan9645_core_set_port_enable(lan9645_core_t *ctx, uint8_t port, uint8_t enable)
{
  uint32_t value = (enable != 0u) ? LAN9645_QSYS_SW_PORT_MODE_PORT_ENA : 0u;
  app_status_t st = lan9645_core_check_ctx(ctx);
  if (st != APP_OK) {
    return st;
  }
  if (port > 9u) {
    return APP_EINVAL;
  }

  return lan9645_reg_rmw32_desc(
    ctx->reg,
    &LAN9645_REG_QSYS_SW_PORT_MODE,
    0u,
    0u,
    (uint32_t)port,
    LAN9645_QSYS_SW_PORT_MODE_PORT_ENA,
    value
  );
}

app_status_t lan9645_core_get_rgmii_link(
  lan9645_core_t *ctx,
  uint8_t rgmii_idx,
  uint8_t *link_up,
  uint8_t *duplex,
  uint8_t *speed_code
)
{
  uint32_t val = 0u;
  app_status_t st = lan9645_core_check_ctx(ctx);
  if (st != APP_OK) {
    return st;
  }
  if (rgmii_idx > 1u) {
    return APP_EINVAL;
  }

  st = lan9645_reg_read32_desc(
    ctx->reg,
    &LAN9645_REG_HSIO_RGMII_CFG,
    0u,
    0u,
    (uint32_t)rgmii_idx,
    &val
  );
  if (st != APP_OK) {
    return st;
  }

  if (link_up != NULL) {
    *link_up = ((val & LAN9645_HSIO_RGMII_CFG_IB_RX_LINK_STATUS) != 0u) ? 1u : 0u;
  }
  if (duplex != NULL) {
    *duplex = ((val & LAN9645_HSIO_RGMII_CFG_IB_RX_DUPLEX) != 0u) ? 1u : 0u;
  }
  if (speed_code != NULL) {
    *speed_code = (uint8_t)LAN9645_FIELD_GET(LAN9645_HSIO_RGMII_CFG_IB_RX_SPEED, val);
  }

  return APP_OK;
}
