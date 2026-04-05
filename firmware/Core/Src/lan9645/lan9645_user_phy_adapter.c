#include "lan9645/lan9645_user_phy_adapter.h"

#include <stddef.h>
#include <string.h>

#include "main.h"

/* Clause-22 style PHY ID value (0x002216D0 split in two 16-bit words). */
#define LAN9645_USER_PHY_ID1_DEFAULT     (0x0022u)
#define LAN9645_USER_PHY_ID2_DEFAULT     (0x16D0u)

#define LAN9645_USER_PHY_ADDR_PORT0      (0u)
#define LAN9645_USER_PHY_ADDR_PORT1      (1u)

#define LAN9645_USER_PHY_ANAR_ALLOWED_MASK \
  (LAN9645_USER_PHY_ANAR_NEXT_PAGE | \
   LAN9645_USER_PHY_ANAR_REMOTE_FAULT | \
   LAN9645_USER_PHY_ANAR_PAUSE_OPERATION | \
   LAN9645_USER_PHY_ANAR_100BASE_TX_FD | \
   LAN9645_USER_PHY_ANAR_100BASE_TX | \
   LAN9645_USER_PHY_ANAR_10BASE_T_FD | \
   LAN9645_USER_PHY_ANAR_10BASE_T | \
   LAN9645_USER_PHY_ANAR_SELECTOR_FIELD)

static lan9645_user_phy_adapter_t *s_bound_ctx = NULL;

static uint16_t lan9645_user_phy_default_anar(void)
{
  return (uint16_t)(
    LAN9645_USER_PHY_ANAR_SELECTOR_FIELD |
    LAN9645_USER_PHY_ANAR_100BASE_TX_FD |
    LAN9645_USER_PHY_ANAR_100BASE_TX |
    LAN9645_USER_PHY_ANAR_10BASE_T_FD |
    LAN9645_USER_PHY_ANAR_10BASE_T
  );
}

static app_status_t lan9645_user_phy_check_ctx(const lan9645_user_phy_adapter_t *ctx)
{
  if (ctx == NULL) {
    return APP_EINVAL;
  }
  if ((ctx->initialized == 0u) || (ctx->core == NULL)) {
    return APP_ESTATE;
  }
  if ((ctx->core->initialized == 0u) || (ctx->core->reg == NULL)) {
    return APP_ESTATE;
  }
  return APP_OK;
}

static app_status_t lan9645_user_phy_check_port(uint8_t port)
{
  if (port >= LAN9645_USER_PHY_PORT_COUNT) {
    return APP_EINVAL;
  }
  return APP_OK;
}

static app_status_t lan9645_user_phy_reset_port(lan9645_user_phy_adapter_t *ctx, uint8_t port)
{
  app_status_t st = lan9645_user_phy_check_port(port);
  if (st != APP_OK) {
    return st;
  }

  ctx->autoneg_enabled[port] = 1u;
  ctx->force_speed_100[port] = 1u;
  ctx->force_full_duplex[port] = 1u;
  ctx->loopback_enabled[port] = 0u;
  ctx->control_shadow[port] = LAN9645_USER_PHY_BCR_AUTONEGO_EN;
  ctx->anar_shadow[port] = lan9645_user_phy_default_anar();
  ctx->anlpar_shadow[port] = lan9645_user_phy_default_anar();
  return APP_OK;
}

static app_status_t lan9645_user_phy_resolve_access_port(
  const lan9645_user_phy_adapter_t *ctx,
  uint32_t phy_addr,
  uint32_t reg_addr,
  uint8_t *port
)
{
  if ((ctx == NULL) || (port == NULL)) {
    return APP_EINVAL;
  }

  /* For PHY-ID probing, expose both logical ports on address 0 and 1. */
  if ((reg_addr == LAN9645_USER_PHY_REG_PHYI1R) || (reg_addr == LAN9645_USER_PHY_REG_PHYI2R)) {
    if (phy_addr == LAN9645_USER_PHY_ADDR_PORT0) {
      *port = LAN9645_USER_PHY_PORT0;
      return APP_OK;
    }
    if (phy_addr == LAN9645_USER_PHY_ADDR_PORT1) {
      *port = LAN9645_USER_PHY_PORT1;
      return APP_OK;
    }
    return APP_EINVAL;
  }

  /* Runtime access uses explicit selected context. */
  *port = ctx->active_port;
  return APP_OK;
}

static app_status_t lan9645_user_phy_sample_link(
  lan9645_user_phy_adapter_t *ctx,
  uint8_t port,
  uint8_t *link_up,
  uint8_t *full_duplex,
  uint8_t *speed_100
)
{
  uint8_t hw_link = 0u;
  uint8_t hw_duplex = 0u;
  uint8_t hw_speed = 0u;
  app_status_t st = APP_OK;

  if ((ctx == NULL) || (link_up == NULL) || (full_duplex == NULL) || (speed_100 == NULL)) {
    return APP_EINVAL;
  }

  if (ctx->autoneg_enabled[port] == 0u) {
    *link_up = 1u;
    *full_duplex = ctx->force_full_duplex[port];
    *speed_100 = ctx->force_speed_100[port];
    return APP_OK;
  }

  /*
   * Current baseline maps logical ports to HSIO_RGMII_CFG indices.
   * This mapping remains provisional until target-map validation is closed.
   */
  st = lan9645_core_get_rgmii_link(ctx->core, port, &hw_link, &hw_duplex, &hw_speed);
  if (st != APP_OK) {
    return st;
  }

  *link_up = (hw_link != 0u) ? 1u : 0u;
  *full_duplex = (hw_duplex != 0u) ? 1u : 0u;

  if (hw_speed == 0u) {
    *speed_100 = 0u;
  } else {
    /* Clamp non-10/100 results to 100 for phase-1 policy. */
    *speed_100 = 1u;
  }

  return APP_OK;
}

static uint16_t lan9645_user_phy_build_bcr(const lan9645_user_phy_adapter_t *ctx, uint8_t port)
{
  uint16_t bcr = 0u;

  if (ctx->autoneg_enabled[port] != 0u) {
    bcr |= LAN9645_USER_PHY_BCR_AUTONEGO_EN;
  }
  if (ctx->force_speed_100[port] != 0u) {
    bcr |= LAN9645_USER_PHY_BCR_SPEED_SELECT;
  }
  if (ctx->force_full_duplex[port] != 0u) {
    bcr |= LAN9645_USER_PHY_BCR_DUPLEX_MODE;
  }
  if (ctx->loopback_enabled[port] != 0u) {
    bcr |= LAN9645_USER_PHY_BCR_LOOPBACK;
  }

  bcr |= (uint16_t)(ctx->control_shadow[port] & (LAN9645_USER_PHY_BCR_POWER_DOWN | LAN9645_USER_PHY_BCR_ISOLATE));
  return bcr;
}

static uint16_t lan9645_user_phy_build_bsr(
  const lan9645_user_phy_adapter_t *ctx,
  uint8_t port,
  uint8_t link_up
)
{
  uint16_t bsr = 0u;

  bsr |= LAN9645_USER_PHY_BSR_100BASE_TX_FD;
  bsr |= LAN9645_USER_PHY_BSR_100BASE_TX_HD;
  bsr |= LAN9645_USER_PHY_BSR_10BASE_T_FD;
  bsr |= LAN9645_USER_PHY_BSR_10BASE_T_HD;
  bsr |= LAN9645_USER_PHY_BSR_AUTONEGO_ABILITY;
  bsr |= LAN9645_USER_PHY_BSR_EXTENDED_CAP;
  bsr |= LAN9645_USER_PHY_BSR_EXTENDED_STATUS;

  if (link_up != 0u) {
    bsr |= LAN9645_USER_PHY_BSR_LINK_STATUS;
    if (ctx->autoneg_enabled[port] != 0u) {
      bsr |= LAN9645_USER_PHY_BSR_AUTONEGO_CPLT;
    }
  }

  return bsr;
}

static uint16_t lan9645_user_phy_build_anlpar(
  const lan9645_user_phy_adapter_t *ctx,
  uint8_t port,
  uint8_t link_up,
  uint8_t full_duplex,
  uint8_t speed_100
)
{
  uint16_t anlpar = (uint16_t)(ctx->anlpar_shadow[port] & LAN9645_USER_PHY_ANAR_ALLOWED_MASK);

  anlpar &= (uint16_t)~(
    LAN9645_USER_PHY_ANAR_100BASE_TX_FD |
    LAN9645_USER_PHY_ANAR_100BASE_TX |
    LAN9645_USER_PHY_ANAR_10BASE_T_FD |
    LAN9645_USER_PHY_ANAR_10BASE_T
  );

  anlpar |= LAN9645_USER_PHY_ANAR_SELECTOR_FIELD;

  if (link_up == 0u) {
    return anlpar;
  }

  if (speed_100 != 0u) {
    anlpar |= (full_duplex != 0u) ? LAN9645_USER_PHY_ANAR_100BASE_TX_FD : LAN9645_USER_PHY_ANAR_100BASE_TX;
  } else {
    anlpar |= (full_duplex != 0u) ? LAN9645_USER_PHY_ANAR_10BASE_T_FD : LAN9645_USER_PHY_ANAR_10BASE_T;
  }

  return anlpar;
}

app_status_t lan9645_user_phy_adapter_init(
  lan9645_user_phy_adapter_t *ctx,
  lan9645_core_t *core
)
{
  app_status_t st = APP_OK;

  if ((ctx == NULL) || (core == NULL)) {
    return APP_EINVAL;
  }

  memset(ctx, 0, sizeof(*ctx));
  ctx->core = core;
  ctx->active_port = LAN9645_USER_PHY_PORT0;
  ctx->phy_id1 = LAN9645_USER_PHY_ID1_DEFAULT;
  ctx->phy_id2 = LAN9645_USER_PHY_ID2_DEFAULT;

  st = lan9645_user_phy_reset_port(ctx, LAN9645_USER_PHY_PORT0);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_user_phy_reset_port(ctx, LAN9645_USER_PHY_PORT1);
  if (st != APP_OK) {
    return st;
  }

  ctx->initialized = 1u;
  return APP_OK;
}

app_status_t lan9645_user_phy_adapter_bind(lan9645_user_phy_adapter_t *ctx)
{
  app_status_t st = lan9645_user_phy_check_ctx(ctx);
  if (st != APP_OK) {
    return st;
  }

  s_bound_ctx = ctx;
  return APP_OK;
}

app_status_t lan9645_user_phy_adapter_unbind(void)
{
  s_bound_ctx = NULL;
  return APP_OK;
}

app_status_t lan9645_user_phy_adapter_set_active_port(lan9645_user_phy_adapter_t *ctx, uint8_t port)
{
  app_status_t st = lan9645_user_phy_check_ctx(ctx);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_user_phy_check_port(port);
  if (st != APP_OK) {
    return st;
  }

  ctx->active_port = port;
  return APP_OK;
}

app_status_t lan9645_user_phy_adapter_get_active_port(
  const lan9645_user_phy_adapter_t *ctx,
  uint8_t *port
)
{
  app_status_t st = lan9645_user_phy_check_ctx(ctx);
  if (st != APP_OK) {
    return st;
  }
  if (port == NULL) {
    return APP_EINVAL;
  }

  *port = ctx->active_port;
  return APP_OK;
}

app_status_t lan9645_user_phy_adapter_read_reg(
  lan9645_user_phy_adapter_t *ctx,
  uint32_t phy_addr,
  uint32_t reg_addr,
  uint32_t *value
)
{
  uint8_t port = 0u;
  uint8_t link_up = 0u;
  uint8_t full_duplex = 0u;
  uint8_t speed_100 = 0u;
  app_status_t st = lan9645_user_phy_check_ctx(ctx);

  if (st != APP_OK) {
    return st;
  }
  if (value == NULL) {
    return APP_EINVAL;
  }

  st = lan9645_user_phy_resolve_access_port(ctx, phy_addr, reg_addr, &port);
  if (st != APP_OK) {
    return st;
  }

  switch (reg_addr) {
    case LAN9645_USER_PHY_REG_BCR:
      *value = (uint32_t)lan9645_user_phy_build_bcr(ctx, port);
      return APP_OK;

    case LAN9645_USER_PHY_REG_BSR:
      st = lan9645_user_phy_sample_link(ctx, port, &link_up, &full_duplex, &speed_100);
      if (st != APP_OK) {
        return st;
      }
      *value = (uint32_t)lan9645_user_phy_build_bsr(ctx, port, link_up);
      return APP_OK;

    case LAN9645_USER_PHY_REG_PHYI1R:
      *value = (uint32_t)ctx->phy_id1;
      return APP_OK;

    case LAN9645_USER_PHY_REG_PHYI2R:
      *value = (uint32_t)ctx->phy_id2;
      return APP_OK;

    case LAN9645_USER_PHY_REG_ANAR:
      *value = (uint32_t)ctx->anar_shadow[port];
      return APP_OK;

    case LAN9645_USER_PHY_REG_ANLPAR:
      st = lan9645_user_phy_sample_link(ctx, port, &link_up, &full_duplex, &speed_100);
      if (st != APP_OK) {
        return st;
      }
      *value = (uint32_t)lan9645_user_phy_build_anlpar(ctx, port, link_up, full_duplex, speed_100);
      return APP_OK;

    default:
      return APP_ENOTSUP;
  }
}

app_status_t lan9645_user_phy_adapter_write_reg(
  lan9645_user_phy_adapter_t *ctx,
  uint32_t phy_addr,
  uint32_t reg_addr,
  uint32_t value
)
{
  uint8_t port = 0u;
  uint16_t regv = (uint16_t)value;
  app_status_t st = lan9645_user_phy_check_ctx(ctx);

  if (st != APP_OK) {
    return st;
  }

  st = lan9645_user_phy_resolve_access_port(ctx, phy_addr, reg_addr, &port);
  if (st != APP_OK) {
    return st;
  }

  switch (reg_addr) {
    case LAN9645_USER_PHY_REG_BCR:
      ctx->control_shadow[port] = (uint16_t)(regv & (LAN9645_USER_PHY_BCR_POWER_DOWN | LAN9645_USER_PHY_BCR_ISOLATE));
      ctx->loopback_enabled[port] = ((regv & LAN9645_USER_PHY_BCR_LOOPBACK) != 0u) ? 1u : 0u;

      if ((regv & LAN9645_USER_PHY_BCR_SOFT_RESET) != 0u) {
        return lan9645_user_phy_reset_port(ctx, port);
      }

      if ((regv & LAN9645_USER_PHY_BCR_RESTART_AUTONEGO) != 0u) {
        ctx->autoneg_enabled[port] = 1u;
      } else if ((regv & LAN9645_USER_PHY_BCR_AUTONEGO_EN) != 0u) {
        ctx->autoneg_enabled[port] = 1u;
      } else {
        ctx->autoneg_enabled[port] = 0u;
      }

      ctx->force_speed_100[port] = ((regv & LAN9645_USER_PHY_BCR_SPEED_SELECT) != 0u) ? 1u : 0u;
      ctx->force_full_duplex[port] = ((regv & LAN9645_USER_PHY_BCR_DUPLEX_MODE) != 0u) ? 1u : 0u;
      return APP_OK;

    case LAN9645_USER_PHY_REG_ANAR:
      if ((regv & (uint16_t)(~LAN9645_USER_PHY_ANAR_ALLOWED_MASK)) != 0u) {
        return APP_ENOTSUP;
      }
      ctx->anar_shadow[port] = (uint16_t)(regv & LAN9645_USER_PHY_ANAR_ALLOWED_MASK);
      return APP_OK;

    case LAN9645_USER_PHY_REG_ANLPAR:
      /* Read-only behavior in the adapter. */
      return APP_ENOTSUP;

    case LAN9645_USER_PHY_REG_PHYI1R:
    case LAN9645_USER_PHY_REG_PHYI2R:
    case LAN9645_USER_PHY_REG_BSR:
      return APP_ENOTSUP;

    default:
      return APP_ENOTSUP;
  }
}

app_status_t lan9645_user_phy_adapter_get_link_state(
  lan9645_user_phy_adapter_t *ctx,
  uint8_t port,
  int32_t *link_state
)
{
  uint8_t link_up = 0u;
  uint8_t full_duplex = 0u;
  uint8_t speed_100 = 0u;
  app_status_t st = lan9645_user_phy_check_ctx(ctx);

  if (st != APP_OK) {
    return st;
  }
  st = lan9645_user_phy_check_port(port);
  if (st != APP_OK) {
    return st;
  }
  if (link_state == NULL) {
    return APP_EINVAL;
  }

  st = lan9645_user_phy_sample_link(ctx, port, &link_up, &full_duplex, &speed_100);
  if (st != APP_OK) {
    return st;
  }

  if (link_up == 0u) {
    *link_state = LAN9645_USER_PHY_LINK_DOWN;
    return APP_OK;
  }

  if ((ctx->autoneg_enabled[port] != 0u) && (ctx->anar_shadow[port] == 0u)) {
    *link_state = LAN9645_USER_PHY_AUTONEGO_NOTDONE;
    return APP_OK;
  }

  if (speed_100 != 0u) {
    *link_state = (full_duplex != 0u) ? LAN9645_USER_PHY_100MBITS_FULLDUPLEX : LAN9645_USER_PHY_100MBITS_HALFDUPLEX;
  } else {
    *link_state = (full_duplex != 0u) ? LAN9645_USER_PHY_10MBITS_FULLDUPLEX : LAN9645_USER_PHY_10MBITS_HALFDUPLEX;
  }

  return APP_OK;
}

int32_t lan9645_user_phy_adapter_io_read_reg(
  uint32_t phy_addr,
  uint32_t reg_addr,
  uint32_t *value
)
{
  if (s_bound_ctx == NULL) {
    return (int32_t)APP_ESTATE;
  }
  return (int32_t)lan9645_user_phy_adapter_read_reg(s_bound_ctx, phy_addr, reg_addr, value);
}

int32_t lan9645_user_phy_adapter_io_write_reg(
  uint32_t phy_addr,
  uint32_t reg_addr,
  uint32_t value
)
{
  if (s_bound_ctx == NULL) {
    return (int32_t)APP_ESTATE;
  }
  return (int32_t)lan9645_user_phy_adapter_write_reg(s_bound_ctx, phy_addr, reg_addr, value);
}

int32_t lan9645_user_phy_adapter_io_get_tick(void)
{
  return (int32_t)HAL_GetTick();
}
