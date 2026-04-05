#include "lan9645/lan9645_lag.h"

/**
 * @file lan9645_lag.c
 * @brief Baseline LAG hash configuration aligned with Linux LAN9645 behavior.
 */

#include "lan9645/lan9645_reg.h"
#include "lan9645/lan9645_regs.h"

static uint8_t s_lag_initialized = 0u;

/**
 * @brief Validates that LAG operations can access an initialized register layer.
 */
static app_status_t lan9645_lag_validate_core(const lan9645_core_t *core)
{
  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }

  return APP_OK;
}

/**
 * @brief Writes `ANA_AGGR_CFG` fields and verifies masked readback.
 */
static app_status_t lan9645_lag_write_verify(lan9645_core_t *core, uint32_t mask, uint32_t value)
{
  uint32_t rd = 0u;
  app_status_t st = lan9645_reg_rmw32_desc(
    core->reg,
    &LAN9645_REG_ANA_AGGR_CFG,
    0u,
    0u,
    0u,
    mask,
    value
  );
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_reg_read32_desc(core->reg, &LAN9645_REG_ANA_AGGR_CFG, 0u, 0u, 0u, &rd);
  if (st != APP_OK) {
    return st;
  }
  if ((rd & mask) != value) {
    return APP_EIO;
  }

  return APP_OK;
}

/**
 * @brief Reads LAG hash runtime configuration.
 */
app_status_t lan9645_lag_get_cfg(lan9645_core_t *core, lan9645_lag_cfg_t *out)
{
  uint32_t rd = 0u;
  app_status_t st = lan9645_lag_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (out == NULL) {
    return APP_EINVAL;
  }

  st = lan9645_reg_read32_desc(core->reg, &LAN9645_REG_ANA_AGGR_CFG, 0u, 0u, 0u, &rd);
  if (st != APP_OK) {
    return st;
  }

  out->ac_rnd_ena = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_AGGR_CFG_AC_RND_ENA, rd);
  out->ac_dmac_ena = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_AGGR_CFG_AC_DMAC_ENA, rd);
  out->ac_smac_ena = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_AGGR_CFG_AC_SMAC_ENA, rd);
  out->ac_ip6_flow_lbl_ena = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_AGGR_CFG_AC_IP6_FLOW_LBL_ENA, rd);
  out->ac_ip6_tcpudp_ena = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_AGGR_CFG_AC_IP6_TCPUDP_ENA, rd);
  out->ac_ip4_sipdip_ena = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_AGGR_CFG_AC_IP4_SIPDIP_ENA, rd);
  out->ac_ip4_tcpudp_ena = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_AGGR_CFG_AC_IP4_TCPUDP_ENA, rd);

  return APP_OK;
}

/**
 * @brief Writes LAG hash runtime configuration with readback verification.
 */
app_status_t lan9645_lag_set_cfg(lan9645_core_t *core, const lan9645_lag_cfg_t *cfg)
{
  const uint32_t mask =
    LAN9645_ANA_AGGR_CFG_AC_RND_ENA |
    LAN9645_ANA_AGGR_CFG_AC_DMAC_ENA |
    LAN9645_ANA_AGGR_CFG_AC_SMAC_ENA |
    LAN9645_ANA_AGGR_CFG_AC_IP6_FLOW_LBL_ENA |
    LAN9645_ANA_AGGR_CFG_AC_IP6_TCPUDP_ENA |
    LAN9645_ANA_AGGR_CFG_AC_IP4_SIPDIP_ENA |
    LAN9645_ANA_AGGR_CFG_AC_IP4_TCPUDP_ENA;
  uint32_t value = 0u;
  app_status_t st = lan9645_lag_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (cfg == NULL) {
    return APP_EINVAL;
  }
  if ((cfg->ac_rnd_ena > LAN9645_LAG_BOOL_MAX) ||
      (cfg->ac_dmac_ena > LAN9645_LAG_BOOL_MAX) ||
      (cfg->ac_smac_ena > LAN9645_LAG_BOOL_MAX) ||
      (cfg->ac_ip6_flow_lbl_ena > LAN9645_LAG_BOOL_MAX) ||
      (cfg->ac_ip6_tcpudp_ena > LAN9645_LAG_BOOL_MAX) ||
      (cfg->ac_ip4_sipdip_ena > LAN9645_LAG_BOOL_MAX) ||
      (cfg->ac_ip4_tcpudp_ena > LAN9645_LAG_BOOL_MAX)) {
    return APP_EINVAL;
  }

  value |= LAN9645_ANA_AGGR_CFG_AC_RND_ENA * (uint32_t)(cfg->ac_rnd_ena != 0u);
  value |= LAN9645_ANA_AGGR_CFG_AC_DMAC_ENA * (uint32_t)(cfg->ac_dmac_ena != 0u);
  value |= LAN9645_ANA_AGGR_CFG_AC_SMAC_ENA * (uint32_t)(cfg->ac_smac_ena != 0u);
  value |= LAN9645_ANA_AGGR_CFG_AC_IP6_FLOW_LBL_ENA * (uint32_t)(cfg->ac_ip6_flow_lbl_ena != 0u);
  value |= LAN9645_ANA_AGGR_CFG_AC_IP6_TCPUDP_ENA * (uint32_t)(cfg->ac_ip6_tcpudp_ena != 0u);
  value |= LAN9645_ANA_AGGR_CFG_AC_IP4_SIPDIP_ENA * (uint32_t)(cfg->ac_ip4_sipdip_ena != 0u);
  value |= LAN9645_ANA_AGGR_CFG_AC_IP4_TCPUDP_ENA * (uint32_t)(cfg->ac_ip4_tcpudp_ena != 0u);

  return lan9645_lag_write_verify(core, mask, value);
}

/**
 * @brief Initializes LAG baseline service state.
 */
app_status_t lan9645_lag_init(void)
{
  s_lag_initialized = 1u;
  return APP_OK;
}

/**
 * @brief Applies deterministic LAG baseline.
 */
app_status_t lan9645_lag_apply_baseline(lan9645_core_t *core)
{
  lan9645_lag_cfg_t cfg = {0};
  app_status_t st = lan9645_lag_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (s_lag_initialized == 0u) {
    (void)lan9645_lag_init();
  }

  cfg.ac_rnd_ena = 0u;
  cfg.ac_dmac_ena = 1u;
  cfg.ac_smac_ena = 1u;
  cfg.ac_ip6_flow_lbl_ena = 0u;
  cfg.ac_ip6_tcpudp_ena = 0u;
  cfg.ac_ip4_sipdip_ena = 0u;
  cfg.ac_ip4_tcpudp_ena = 0u;
  return lan9645_lag_set_cfg(core, &cfg);
}
