#include "lan9645/lan9645_npi.h"

/**
 * @file lan9645_npi.c
 * @brief Baseline NPI-domain configuration aligned with Linux LAN9645 behavior.
 */

#include "lan9645/lan9645_reg.h"
#include "lan9645/lan9645_regs.h"

#define LAN9645_NPI_PORT                 (8u)
#define LAN9645_NPI_CPU_PORT             (8u)
#define LAN9645_NPI_PGID_SRC_BASE        (80u)
#define LAN9645_NPI_PGID_SRC_CPU         (LAN9645_NPI_PGID_SRC_BASE + LAN9645_NPI_CPU_PORT)
#define LAN9645_NPI_TAG_PREFIX_NONE      (1u)
#define LAN9645_NPI_TAG_PREFIX_LONG      (3u)
#define LAN9645_NPI_TAG_CFG_DISABLED     (0u)

static uint8_t s_npi_initialized = 0u;

/**
 * @brief Validates that NPI operations can access a ready core/register layer.
 */
static app_status_t lan9645_npi_validate_core(const lan9645_core_t *core)
{
  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }

  return APP_OK;
}

/**
 * @brief Validates one NPI port index.
 */
static app_status_t lan9645_npi_validate_port(uint8_t port)
{
  if (port > LAN9645_NPI_PORT_MAX) {
    return APP_EINVAL;
  }

  return APP_OK;
}

/**
 * @brief Performs one descriptor RMW and verifies masked readback.
 */
static app_status_t lan9645_npi_rmw_verify(
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
 * @brief Reads NPI runtime configuration.
 */
app_status_t lan9645_npi_get_cfg(lan9645_core_t *core, lan9645_npi_cfg_t *out)
{
  uint32_t ext = 0u;
  uint32_t sys = 0u;
  uint32_t rew = 0u;
  uint32_t pgid = 0u;
  uint8_t port = 0u;
  app_status_t st = lan9645_npi_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (out == NULL) {
    return APP_EINVAL;
  }

  st = lan9645_reg_read32_desc(
    core->reg,
    &LAN9645_REG_QSYS_EXT_CPU_CFG,
    0u,
    0u,
    0u,
    &ext
  );
  if (st != APP_OK) {
    return st;
  }

  port = (uint8_t)LAN9645_FIELD_GET(LAN9645_QSYS_EXT_CPU_CFG_EXT_CPU_PORT, ext);
  st = lan9645_npi_validate_port(port);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_reg_read32_desc(
    core->reg,
    &LAN9645_REG_SYS_PORT_MODE,
    0u,
    0u,
    (uint32_t)port,
    &sys
  );
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_reg_read32_desc(
    core->reg,
    &LAN9645_REG_REW_TAG_CFG,
    0u,
    (uint32_t)port,
    0u,
    &rew
  );
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_reg_read32_desc(
    core->reg,
    &LAN9645_REG_ANA_PGID,
    0u,
    LAN9645_NPI_PGID_SRC_BASE + (uint32_t)port,
    0u,
    &pgid
  );
  if (st != APP_OK) {
    return st;
  }

  out->npi_port = port;
  out->ext_cpuq_mask = (uint8_t)LAN9645_FIELD_GET(LAN9645_QSYS_EXT_CPU_CFG_EXT_CPUQ_MSK, ext);
  out->ext_cpu_kill = (uint8_t)LAN9645_FIELD_GET(LAN9645_QSYS_EXT_CPU_CFG_EXT_CPU_KILL_ENA, ext);
  out->int_cpu_kill = (uint8_t)LAN9645_FIELD_GET(LAN9645_QSYS_EXT_CPU_CFG_INT_CPU_KILL_ENA, ext);
  out->incl_xtr_hdr = (uint8_t)LAN9645_FIELD_GET(LAN9645_SYS_PORT_MODE_INCL_XTR_HDR, sys);
  out->incl_inj_hdr = (uint8_t)LAN9645_FIELD_GET(LAN9645_SYS_PORT_MODE_INCL_INJ_HDR, sys);
  out->rew_tag_cfg = (uint8_t)LAN9645_FIELD_GET(LAN9645_REW_TAG_CFG_TAG_CFG, rew);
  out->src_pgid_mask = (uint16_t)LAN9645_FIELD_GET(LAN9645_ANA_PGID_PGID, pgid);

  return APP_OK;
}

/**
 * @brief Writes NPI runtime configuration with readback verification.
 */
app_status_t lan9645_npi_set_cfg(lan9645_core_t *core, const lan9645_npi_cfg_t *cfg)
{
  const uint32_t ext_mask =
    LAN9645_QSYS_EXT_CPU_CFG_EXT_CPUQ_MSK |
    LAN9645_QSYS_EXT_CPU_CFG_EXT_CPU_PORT |
    LAN9645_QSYS_EXT_CPU_CFG_EXT_CPU_KILL_ENA |
    LAN9645_QSYS_EXT_CPU_CFG_INT_CPU_KILL_ENA;
  const uint32_t ext_value =
    LAN9645_QSYS_EXT_CPU_CFG_EXT_CPUQ_MSK_SET((uint32_t)cfg->ext_cpuq_mask) |
    LAN9645_QSYS_EXT_CPU_CFG_EXT_CPU_PORT_SET((uint32_t)cfg->npi_port) |
    LAN9645_QSYS_EXT_CPU_CFG_EXT_CPU_KILL_ENA * (uint32_t)(cfg->ext_cpu_kill != 0u) |
    LAN9645_QSYS_EXT_CPU_CFG_INT_CPU_KILL_ENA * (uint32_t)(cfg->int_cpu_kill != 0u);
  const uint32_t ifh_mask =
    LAN9645_SYS_PORT_MODE_INCL_XTR_HDR |
    LAN9645_SYS_PORT_MODE_INCL_INJ_HDR;
  const uint32_t ifh_value =
    LAN9645_SYS_PORT_MODE_INCL_XTR_HDR_SET((uint32_t)cfg->incl_xtr_hdr) |
    LAN9645_SYS_PORT_MODE_INCL_INJ_HDR_SET((uint32_t)cfg->incl_inj_hdr);
  const uint32_t rew_mask = LAN9645_REW_TAG_CFG_TAG_CFG;
  const uint32_t rew_value = LAN9645_REW_TAG_CFG_TAG_CFG_SET((uint32_t)cfg->rew_tag_cfg);
  const uint32_t pgid_mask = LAN9645_ANA_PGID_PGID;
  const uint32_t pgid_value = LAN9645_ANA_PGID_PGID_SET((uint32_t)cfg->src_pgid_mask);
  app_status_t st = lan9645_npi_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (cfg == NULL) {
    return APP_EINVAL;
  }
  st = lan9645_npi_validate_port(cfg->npi_port);
  if (st != APP_OK) {
    return st;
  }
  if ((cfg->ext_cpuq_mask > LAN9645_NPI_CPUQ_MASK_MAX) ||
      (cfg->ext_cpu_kill > 1u) ||
      (cfg->int_cpu_kill > 1u) ||
      (cfg->incl_xtr_hdr > LAN9645_NPI_IFH_PREFIX_MAX) ||
      (cfg->incl_inj_hdr > LAN9645_NPI_IFH_PREFIX_MAX) ||
      (cfg->rew_tag_cfg > LAN9645_NPI_REW_TAG_CFG_MAX) ||
      (cfg->src_pgid_mask > LAN9645_NPI_PGID_MASK_MAX)) {
    return APP_EINVAL;
  }

  st = lan9645_npi_rmw_verify(core, &LAN9645_REG_QSYS_EXT_CPU_CFG, 0u, 0u, ext_mask, ext_value);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_npi_rmw_verify(
    core,
    &LAN9645_REG_SYS_PORT_MODE,
    0u,
    (uint32_t)cfg->npi_port,
    ifh_mask,
    ifh_value
  );
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_npi_rmw_verify(
    core,
    &LAN9645_REG_REW_TAG_CFG,
    (uint32_t)cfg->npi_port,
    0u,
    rew_mask,
    rew_value
  );
  if (st != APP_OK) {
    return st;
  }

  return lan9645_npi_rmw_verify(
    core,
    &LAN9645_REG_ANA_PGID,
    LAN9645_NPI_PGID_SRC_BASE + (uint32_t)cfg->npi_port,
    0u,
    pgid_mask,
    pgid_value
  );
}

/**
 * @brief Initializes NPI baseline service state.
 */
app_status_t lan9645_npi_init(void)
{
  s_npi_initialized = 1u;
  return APP_OK;
}

/**
 * @brief Applies deterministic NPI baseline.
 */
app_status_t lan9645_npi_apply_baseline(lan9645_core_t *core)
{
  lan9645_npi_cfg_t cfg = {0};
  app_status_t st = lan9645_npi_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (s_npi_initialized == 0u) {
    (void)lan9645_npi_init();
  }

  st = lan9645_npi_get_cfg(core, &cfg);
  if (st != APP_OK) {
    return st;
  }

  cfg.npi_port = LAN9645_NPI_PORT;
  cfg.ext_cpuq_mask = 0xFFu;
  cfg.ext_cpu_kill = 1u;
  cfg.int_cpu_kill = 1u;
  cfg.incl_xtr_hdr = LAN9645_NPI_TAG_PREFIX_LONG;
  cfg.incl_inj_hdr = LAN9645_NPI_TAG_PREFIX_NONE;
  cfg.rew_tag_cfg = LAN9645_NPI_TAG_CFG_DISABLED;
  cfg.src_pgid_mask = (uint16_t)(cfg.src_pgid_mask & (uint16_t)(~LAN9645_BIT(cfg.npi_port)));

  return lan9645_npi_set_cfg(core, &cfg);
}
