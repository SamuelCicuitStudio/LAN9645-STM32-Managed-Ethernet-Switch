#include "lan9645/lan9645_hsr_prp.h"

/**
 * @file lan9645_hsr_prp.c
 * @brief Baseline HSR/PRP-domain configuration aligned with Linux LAN9645 behavior.
 */

#include "lan9645/lan9645_reg.h"
#include "lan9645/lan9645_regs.h"

#define LAN9645_HSR_PRP_PGID_AGGR  (64u)
#define LAN9645_HSR_PRP_PGID_CPU   (LAN9645_HSR_PRP_PGID_AGGR - 6u)
#define LAN9645_HSR_PRP_CPU_PORT   (8u)

static uint8_t s_hsr_prp_initialized = 0u;

/**
 * @brief Validates that HSR/PRP operations can access a ready core/register layer.
 */
static app_status_t lan9645_hsr_prp_validate_core(const lan9645_core_t *core)
{
  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }

  return APP_OK;
}

/**
 * @brief Writes one descriptor field and verifies masked readback.
 */
static app_status_t lan9645_hsr_prp_write_verify(
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
 * @brief Initializes HSR/PRP baseline service state.
 */
app_status_t lan9645_hsr_prp_init(void)
{
  s_hsr_prp_initialized = 1u;
  return APP_OK;
}

/**
 * @brief Reads HSR/PRP runtime configuration.
 */
app_status_t lan9645_hsr_prp_get_cfg(lan9645_core_t *core, lan9645_hsr_prp_cfg_t *out)
{
  uint32_t rd = 0u;
  app_status_t st = lan9645_hsr_prp_validate_core(core);
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
    LAN9645_HSR_PRP_PGID_CPU,
    0u,
    &rd
  );
  if (st != APP_OK) {
    return st;
  }

  out->pgid_cpu_mask = (uint16_t)LAN9645_FIELD_GET(LAN9645_ANA_PGID_PGID, rd);
  return APP_OK;
}

/**
 * @brief Writes HSR/PRP runtime configuration with readback verification.
 */
app_status_t lan9645_hsr_prp_set_cfg(lan9645_core_t *core, const lan9645_hsr_prp_cfg_t *cfg)
{
  app_status_t st = lan9645_hsr_prp_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (cfg == NULL) {
    return APP_EINVAL;
  }
  if (cfg->pgid_cpu_mask > LAN9645_HSR_PRP_PGID_MASK_MAX) {
    return APP_EINVAL;
  }

  return lan9645_hsr_prp_write_verify(
    core,
    &LAN9645_REG_ANA_PGID,
    LAN9645_HSR_PRP_PGID_CPU,
    0u,
    LAN9645_ANA_PGID_PGID,
    LAN9645_ANA_PGID_PGID_SET((uint32_t)cfg->pgid_cpu_mask)
  );
}

/**
 * @brief Applies deterministic HSR/PRP baseline.
 */
app_status_t lan9645_hsr_prp_apply_baseline(lan9645_core_t *core)
{
  lan9645_hsr_prp_cfg_t cfg = {0};
  app_status_t st = lan9645_hsr_prp_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (s_hsr_prp_initialized == 0u) {
    (void)lan9645_hsr_prp_init();
  }

  cfg.pgid_cpu_mask = (uint16_t)LAN9645_BIT(LAN9645_HSR_PRP_CPU_PORT);
  return lan9645_hsr_prp_set_cfg(core, &cfg);
}
