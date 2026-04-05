#include "lan9645/lan9645_manual_inj.h"

/**
 * @file lan9645_manual_inj.c
 * @brief Manual-injection configuration aligned with Linux LAN9645 behavior.
 */

#include "lan9645/lan9645_reg.h"
#include "lan9645/lan9645_regs.h"

static uint8_t s_manual_inj_initialized = 0u;

/**
 * @brief Validates that manual-injection operations can access a ready core/register layer.
 */
static app_status_t lan9645_manual_inj_validate_core(const lan9645_core_t *core)
{
  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }

  return APP_OK;
}

/**
 * @brief Validates manual-injection port selector.
 */
static app_status_t lan9645_manual_inj_validate_port(uint8_t port)
{
  if (port > LAN9645_MANUAL_INJ_PORT_MAX) {
    return APP_EINVAL;
  }

  return APP_OK;
}

/**
 * @brief Initializes manual-injection service state.
 */
app_status_t lan9645_manual_inj_init(void)
{
  s_manual_inj_initialized = 1u;
  return APP_OK;
}

/**
 * @brief Reads manual-injection configuration for one port.
 */
app_status_t lan9645_manual_inj_get_cfg(
  lan9645_core_t *core,
  uint8_t port,
  lan9645_manual_inj_cfg_t *out
)
{
  uint32_t rd = 0u;
  app_status_t st = lan9645_manual_inj_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_manual_inj_validate_port(port);
  if (st != APP_OK) {
    return st;
  }
  if (out == NULL) {
    return APP_EINVAL;
  }

  st = lan9645_reg_read32_desc(
    core->reg,
    &LAN9645_REG_SYS_PORT_MODE,
    0u,
    0u,
    (uint32_t)port,
    &rd
  );
  if (st != APP_OK) {
    return st;
  }

  out->port = port;
  out->incl_inj_hdr = (uint8_t)LAN9645_FIELD_GET(LAN9645_SYS_PORT_MODE_INCL_INJ_HDR, rd);
  return APP_OK;
}

/**
 * @brief Writes manual-injection configuration with readback verification.
 */
app_status_t lan9645_manual_inj_set_cfg(
  lan9645_core_t *core,
  const lan9645_manual_inj_cfg_t *cfg
)
{
  uint32_t rd = 0u;
  uint32_t value = 0u;
  app_status_t st = lan9645_manual_inj_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (cfg == NULL) {
    return APP_EINVAL;
  }
  st = lan9645_manual_inj_validate_port(cfg->port);
  if (st != APP_OK) {
    return st;
  }
  if (cfg->incl_inj_hdr > LAN9645_MANUAL_INJ_INCL_INJ_HDR_MAX) {
    return APP_EINVAL;
  }
  value = LAN9645_SYS_PORT_MODE_INCL_INJ_HDR_SET((uint32_t)cfg->incl_inj_hdr);

  st = lan9645_reg_rmw32_desc(
    core->reg,
    &LAN9645_REG_SYS_PORT_MODE,
    0u,
    0u,
    (uint32_t)cfg->port,
    LAN9645_SYS_PORT_MODE_INCL_INJ_HDR,
    value
  );
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_reg_read32_desc(
    core->reg,
    &LAN9645_REG_SYS_PORT_MODE,
    0u,
    0u,
    (uint32_t)cfg->port,
    &rd
  );
  if (st != APP_OK) {
    return st;
  }
  if ((rd & LAN9645_SYS_PORT_MODE_INCL_INJ_HDR) != value) {
    return APP_EIO;
  }

  return APP_OK;
}

/**
 * @brief Applies deterministic manual-injection baseline.
 */
app_status_t lan9645_manual_inj_apply_baseline(lan9645_core_t *core)
{
  lan9645_manual_inj_cfg_t cfg = {0};
  app_status_t st = lan9645_manual_inj_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (s_manual_inj_initialized == 0u) {
    (void)lan9645_manual_inj_init();
  }

  cfg.port = LAN9645_MANUAL_INJ_DEFAULT_PORT;
  cfg.incl_inj_hdr = 1u;
  return lan9645_manual_inj_set_cfg(core, &cfg);
}
