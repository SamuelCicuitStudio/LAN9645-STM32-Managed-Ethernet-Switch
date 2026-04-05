#include "lan9645/lan9645_mrp.h"

/**
 * @file lan9645_mrp.c
 * @brief Baseline MRP-domain configuration aligned with Linux LAN9645 behavior.
 */

#include "lan9645/lan9645_reg.h"
#include "lan9645/lan9645_regs.h"

#define LAN9645_MRP_PGID_AGGR  (64u)
#define LAN9645_MRP_PGID_MRP   (LAN9645_MRP_PGID_AGGR - 7u)

static uint8_t s_mrp_initialized = 0u;

/**
 * @brief Validates that MRP operations can access a ready core/register layer.
 */
static app_status_t lan9645_mrp_validate_core(const lan9645_core_t *core)
{
  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }

  return APP_OK;
}

/**
 * @brief Writes one descriptor field and verifies masked readback.
 */
static app_status_t lan9645_mrp_write_verify(
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

  st = lan9645_reg_read32_desc(
    core->reg,
    desc,
    0u,
    group_instance,
    reg_instance,
    &rd
  );
  if (st != APP_OK) {
    return st;
  }

  if ((rd & mask) != value) {
    return APP_EIO;
  }

  return APP_OK;
}

/**
 * @brief Initializes MRP baseline service state.
 */
app_status_t lan9645_mrp_init(void)
{
  s_mrp_initialized = 1u;
  return APP_OK;
}

/**
 * @brief Reads MRP runtime configuration.
 */
app_status_t lan9645_mrp_get_cfg(lan9645_core_t *core, lan9645_mrp_cfg_t *out)
{
  uint32_t rd = 0u;
  app_status_t st = lan9645_mrp_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (out == NULL) {
    return APP_EINVAL;
  }

  st = lan9645_reg_read32_desc(
    core->reg,
    &LAN9645_REG_ANA_PGID,
    0u,
    LAN9645_MRP_PGID_MRP,
    0u,
    &rd
  );
  if (st != APP_OK) {
    return st;
  }

  out->pgid_mrp_mask = (uint16_t)LAN9645_FIELD_GET(LAN9645_ANA_PGID_PGID, rd);
  return APP_OK;
}

/**
 * @brief Writes MRP runtime configuration with readback verification.
 */
app_status_t lan9645_mrp_set_cfg(lan9645_core_t *core, const lan9645_mrp_cfg_t *cfg)
{
  app_status_t st = lan9645_mrp_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (cfg == NULL) {
    return APP_EINVAL;
  }
  if (cfg->pgid_mrp_mask > LAN9645_MRP_MEMBER_MASK_MAX) {
    return APP_EINVAL;
  }

  return lan9645_mrp_write_verify(
    core,
    &LAN9645_REG_ANA_PGID,
    LAN9645_MRP_PGID_MRP,
    0u,
    LAN9645_ANA_PGID_PGID,
    LAN9645_ANA_PGID_PGID_SET((uint32_t)cfg->pgid_mrp_mask)
  );
}

/**
 * @brief Applies deterministic MRP baseline.
 */
app_status_t lan9645_mrp_apply_baseline(lan9645_core_t *core)
{
  lan9645_mrp_cfg_t cfg = {0};
  app_status_t st = lan9645_mrp_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (s_mrp_initialized == 0u) {
    (void)lan9645_mrp_init();
  }

  cfg.pgid_mrp_mask = 0u;
  return lan9645_mrp_set_cfg(core, &cfg);
}
