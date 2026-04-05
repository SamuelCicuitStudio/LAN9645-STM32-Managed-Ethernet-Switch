#include "lan9645/lan9645_cut_thru.h"

/**
 * @file lan9645_cut_thru.c
 * @brief Baseline cut-through disable path aligned with Linux LAN9645 managed behavior.
 */

#include "lan9645/lan9645_reg.h"
#include "lan9645/lan9645_regs.h"

static const uint8_t s_cut_thru_ports[] = {0u, 1u, 8u};
static uint8_t s_cut_thru_initialized = 0u;

/**
 * @brief Validates that cut-through operations can access an initialized register layer.
 */
static app_status_t lan9645_cut_thru_validate_core(const lan9645_core_t *core)
{
  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  return APP_OK;
}

/**
 * @brief Validates encoded cut-through field values before register programming.
 */
static app_status_t lan9645_cut_thru_validate_cfg_values(uint8_t cut_thru_spd, uint8_t cut_thru_ena_mask)
{
  if ((cut_thru_spd > LAN9645_CUT_THRU_SPD_MAX) || (cut_thru_ena_mask > LAN9645_CUT_THRU_ENA_MASK_MAX)) {
    return APP_EINVAL;
  }
  return APP_OK;
}

/**
 * @brief Performs a descriptor RMW and verifies masked readback.
 */
static app_status_t lan9645_cut_thru_rmw_verify(
  lan9645_core_t *core,
  const lan9645_reg_desc_t *desc,
  uint32_t group_instance,
  uint32_t reg_instance,
  uint32_t mask,
  uint32_t value
)
{
  uint32_t rd = 0u;
  app_status_t st = lan9645_reg_rmw32_desc(
    core->reg,
    desc,
    0u,
    group_instance,
    reg_instance,
    mask,
    value
  );
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_reg_read32_desc(core->reg, desc, 0u, group_instance, reg_instance, &rd);
  if (st != APP_OK) {
    return st;
  }

  if ((rd & mask) != (value & mask)) {
    return APP_EIO;
  }
  return APP_OK;
}

/**
 * @brief Initializes cut-through baseline service state.
 */
app_status_t lan9645_cut_thru_init(void)
{
  s_cut_thru_initialized = 1u;
  return APP_OK;
}

/**
 * @brief Applies deterministic cut-through baseline for managed mode.
 */
app_status_t lan9645_cut_thru_apply_baseline(lan9645_core_t *core)
{
  app_status_t st = lan9645_cut_thru_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (s_cut_thru_initialized == 0u) {
    (void)lan9645_cut_thru_init();
  }

  for (uint32_t i = 0u; i < (uint32_t)(sizeof(s_cut_thru_ports) / sizeof(s_cut_thru_ports[0])); ++i) {
    st = lan9645_cut_thru_set_cfg(core, s_cut_thru_ports[i], 0u, 0u);
    if (st != APP_OK) {
      return st;
    }
  }

  return APP_OK;
}

/**
 * @brief Reads one port cut-through configuration with decoded field values.
 */
app_status_t lan9645_cut_thru_get_cfg(
  lan9645_core_t *core,
  uint8_t port,
  lan9645_cut_thru_cfg_t *out
)
{
  uint32_t rd = 0u;
  app_status_t st = lan9645_cut_thru_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (out == NULL) {
    return APP_EINVAL;
  }

  st = lan9645_reg_read32_desc(
    core->reg,
    &LAN9645_REG_ANA_CUT_THRU_CFG,
    0u,
    (uint32_t)port,
    0u,
    &rd
  );
  if (st != APP_OK) {
    return st;
  }

  out->port = port;
  out->cut_thru_spd = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_CUT_THRU_CFG_CUT_THRU_SPD, rd);
  out->cut_thru_ena_mask = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_CUT_THRU_CFG_CUT_THRU_ENA, rd);
  return APP_OK;
}

/**
 * @brief Programs one port cut-through configuration and verifies register readback.
 */
app_status_t lan9645_cut_thru_set_cfg(
  lan9645_core_t *core,
  uint8_t port,
  uint8_t cut_thru_spd,
  uint8_t cut_thru_ena_mask
)
{
  const uint32_t cut_thru_mask =
    LAN9645_ANA_CUT_THRU_CFG_CUT_THRU_SPD |
    LAN9645_ANA_CUT_THRU_CFG_CUT_THRU_ENA;
  app_status_t st = lan9645_cut_thru_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_cut_thru_validate_cfg_values(cut_thru_spd, cut_thru_ena_mask);
  if (st != APP_OK) {
    return st;
  }

  return lan9645_cut_thru_rmw_verify(
    core,
    &LAN9645_REG_ANA_CUT_THRU_CFG,
    (uint32_t)port,
    0u,
    cut_thru_mask,
    LAN9645_ANA_CUT_THRU_CFG_CUT_THRU_SPD_SET(cut_thru_spd) |
      LAN9645_ANA_CUT_THRU_CFG_CUT_THRU_ENA_SET(cut_thru_ena_mask)
  );
}
