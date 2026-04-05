#include "lan9645/lan9645_afi.h"

/**
 * @file lan9645_afi.c
 * @brief AFI-domain configuration aligned with Linux LAN9645 behavior.
 */

#include "lan9645/lan9645_reg.h"
#include "lan9645/lan9645_regs.h"

static uint8_t s_afi_initialized = 0u;

/**
 * @brief Validates that AFI operations can access a ready core/register layer.
 */
static app_status_t lan9645_afi_validate_core(const lan9645_core_t *core)
{
  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }

  return APP_OK;
}

/**
 * @brief Initializes AFI service state.
 */
app_status_t lan9645_afi_init(void)
{
  s_afi_initialized = 1u;
  return APP_OK;
}

/**
 * @brief Reads AFI runtime configuration.
 */
app_status_t lan9645_afi_get_cfg(lan9645_core_t *core, lan9645_afi_cfg_t *out)
{
  uint32_t rd = 0u;
  app_status_t st = lan9645_afi_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (out == NULL) {
    return APP_EINVAL;
  }

  st = lan9645_reg_read32_desc(
    core->reg,
    &LAN9645_REG_AFI_MISC_CTRL,
    0u,
    0u,
    0u,
    &rd
  );
  if (st != APP_OK) {
    return st;
  }

  out->afi_enable = (uint8_t)LAN9645_FIELD_GET(LAN9645_AFI_MISC_CTRL_AFI_ENA, rd);
  return APP_OK;
}

/**
 * @brief Writes AFI runtime configuration with readback verification.
 */
app_status_t lan9645_afi_set_cfg(lan9645_core_t *core, const lan9645_afi_cfg_t *cfg)
{
  uint32_t rd = 0u;
  uint32_t value = 0u;
  app_status_t st = lan9645_afi_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (cfg == NULL) {
    return APP_EINVAL;
  }
  if (cfg->afi_enable > LAN9645_AFI_BOOL_MAX) {
    return APP_EINVAL;
  }
  value = LAN9645_AFI_MISC_CTRL_AFI_ENA * (uint32_t)(cfg->afi_enable != 0u);

  st = lan9645_reg_rmw32_desc(
    core->reg,
    &LAN9645_REG_AFI_MISC_CTRL,
    0u,
    0u,
    0u,
    LAN9645_AFI_MISC_CTRL_AFI_ENA,
    value
  );
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_reg_read32_desc(
    core->reg,
    &LAN9645_REG_AFI_MISC_CTRL,
    0u,
    0u,
    0u,
    &rd
  );
  if (st != APP_OK) {
    return st;
  }
  if ((rd & LAN9645_AFI_MISC_CTRL_AFI_ENA) != value) {
    return APP_EIO;
  }

  return APP_OK;
}

/**
 * @brief Applies deterministic AFI baseline.
 */
app_status_t lan9645_afi_apply_baseline(lan9645_core_t *core)
{
  lan9645_afi_cfg_t cfg = {0};
  app_status_t st = lan9645_afi_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (s_afi_initialized == 0u) {
    (void)lan9645_afi_init();
  }

  cfg.afi_enable = 1u;
  return lan9645_afi_set_cfg(core, &cfg);
}
