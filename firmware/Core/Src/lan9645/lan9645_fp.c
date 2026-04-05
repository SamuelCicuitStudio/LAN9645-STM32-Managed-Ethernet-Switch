#include "lan9645/lan9645_fp.h"

/**
 * @file lan9645_fp.c
 * @brief Baseline frame-preemption control aligned with Linux LAN9645 FP/MM init behavior.
 */

#include "lan9645/lan9645_reg.h"
#include "lan9645/lan9645_regs.h"

static const uint8_t s_fp_ports[] = {0u, 1u, 8u};
static uint8_t s_fp_initialized = 0u;
static const uint8_t s_fp_port_max = 8u;

/**
 * @brief Validates that FP operations can access an initialized register layer.
 */
static app_status_t lan9645_fp_validate_core(const lan9645_core_t *core)
{
  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }

  return APP_OK;
}

/**
 * @brief Validates one DEV/QSYS/SYS front-port index for FP operations.
 */
static app_status_t lan9645_fp_validate_port(uint8_t port)
{
  if (port > s_fp_port_max) {
    return APP_EINVAL;
  }

  return APP_OK;
}

/**
 * @brief Performs one descriptor RMW and verifies masked readback.
 */
static app_status_t lan9645_fp_rmw_verify(
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
 * @brief Reads one per-port FP/MM configuration snapshot.
 */
app_status_t lan9645_fp_get_port_cfg(
  lan9645_core_t *core,
  uint8_t port,
  lan9645_fp_port_cfg_t *out
)
{
  uint32_t rd_enable = 0u;
  uint32_t rd_verif = 0u;
  uint32_t rd_front = 0u;
  uint32_t rd_preempt = 0u;
  app_status_t st = APP_OK;

  st = lan9645_fp_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_fp_validate_port(port);
  if (st != APP_OK) {
    return st;
  }
  if (out == NULL) {
    return APP_EINVAL;
  }

  st = lan9645_reg_read32_desc(
    core->reg,
    &LAN9645_REG_DEV_ENABLE_CONFIG,
    0u,
    (uint32_t)port,
    0u,
    &rd_enable
  );
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_reg_read32_desc(
    core->reg,
    &LAN9645_REG_DEV_VERIF_CONFIG,
    0u,
    (uint32_t)port,
    0u,
    &rd_verif
  );
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_reg_read32_desc(
    core->reg,
    &LAN9645_REG_SYS_FRONT_PORT_MODE,
    0u,
    0u,
    (uint32_t)port,
    &rd_front
  );
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_reg_read32_desc(
    core->reg,
    &LAN9645_REG_QSYS_PREEMPT_CFG,
    0u,
    0u,
    (uint32_t)port,
    &rd_preempt
  );
  if (st != APP_OK) {
    return st;
  }

  out->port = port;
  out->mm_rx_enable = (uint8_t)LAN9645_FIELD_GET(LAN9645_DEV_ENABLE_CONFIG_MM_RX_ENA, rd_enable);
  out->mm_tx_enable = (uint8_t)LAN9645_FIELD_GET(LAN9645_DEV_ENABLE_CONFIG_MM_TX_ENA, rd_enable);
  out->keep_s_after_d = (uint8_t)LAN9645_FIELD_GET(LAN9645_DEV_ENABLE_CONFIG_KEEP_S_AFTER_D, rd_enable);
  out->verify_disable = (uint8_t)LAN9645_FIELD_GET(LAN9645_DEV_VERIF_CONFIG_PRM_VERIFY_DIS, rd_verif);
  out->verify_time = (uint8_t)LAN9645_FIELD_GET(LAN9645_DEV_VERIF_CONFIG_PRM_VERIFY_TIME, rd_verif);
  out->verify_timer_units = (uint8_t)LAN9645_FIELD_GET(LAN9645_DEV_VERIF_CONFIG_VERIF_TIMER_UNITS, rd_verif);
  out->add_frag_size = (uint8_t)LAN9645_FIELD_GET(LAN9645_SYS_FRONT_PORT_MODE_ADD_FRAG_SIZE, rd_front);
  out->preempt_queue_mask = (uint8_t)LAN9645_FIELD_GET(LAN9645_QSYS_PREEMPT_CFG_P_QUEUES, rd_preempt);
  out->strict_ipg = (uint8_t)LAN9645_FIELD_GET(LAN9645_QSYS_PREEMPT_CFG_STRICT_IPG, rd_preempt);

  return APP_OK;
}

/**
 * @brief Writes one per-port FP/MM configuration.
 */
app_status_t lan9645_fp_set_port_cfg(
  lan9645_core_t *core,
  uint8_t port,
  const lan9645_fp_port_cfg_t *cfg
)
{
  app_status_t st = lan9645_fp_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_fp_validate_port(port);
  if (st != APP_OK) {
    return st;
  }
  if (cfg == NULL) {
    return APP_EINVAL;
  }
  if ((cfg->mm_rx_enable > 1u) ||
      (cfg->mm_tx_enable > 1u) ||
      (cfg->keep_s_after_d > 1u) ||
      (cfg->verify_disable > 1u) ||
      (cfg->verify_time > LAN9645_FP_VERIFY_TIME_MAX) ||
      (cfg->verify_timer_units > LAN9645_FP_VERIFY_UNITS_MAX) ||
      (cfg->add_frag_size > LAN9645_FP_ADD_FRAG_SIZE_MAX) ||
      (cfg->preempt_queue_mask > LAN9645_FP_PREEMPT_QUEUE_MASK_MAX) ||
      (cfg->strict_ipg > LAN9645_FP_STRICT_IPG_MAX)) {
    return APP_EINVAL;
  }

  st = lan9645_fp_rmw_verify(
    core,
    &LAN9645_REG_DEV_ENABLE_CONFIG,
    (uint32_t)port,
    0u,
    LAN9645_DEV_ENABLE_CONFIG_MM_RX_ENA |
      LAN9645_DEV_ENABLE_CONFIG_MM_TX_ENA |
      LAN9645_DEV_ENABLE_CONFIG_KEEP_S_AFTER_D,
    LAN9645_DEV_ENABLE_CONFIG_MM_RX_ENA_SET((uint32_t)cfg->mm_rx_enable) |
      LAN9645_DEV_ENABLE_CONFIG_MM_TX_ENA_SET((uint32_t)cfg->mm_tx_enable) |
      LAN9645_DEV_ENABLE_CONFIG_KEEP_S_AFTER_D_SET((uint32_t)cfg->keep_s_after_d)
  );
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_fp_rmw_verify(
    core,
    &LAN9645_REG_DEV_VERIF_CONFIG,
    (uint32_t)port,
    0u,
    LAN9645_DEV_VERIF_CONFIG_PRM_VERIFY_DIS |
      LAN9645_DEV_VERIF_CONFIG_PRM_VERIFY_TIME |
      LAN9645_DEV_VERIF_CONFIG_VERIF_TIMER_UNITS,
    LAN9645_DEV_VERIF_CONFIG_PRM_VERIFY_DIS_SET((uint32_t)cfg->verify_disable) |
      LAN9645_DEV_VERIF_CONFIG_PRM_VERIFY_TIME_SET((uint32_t)cfg->verify_time) |
      LAN9645_DEV_VERIF_CONFIG_VERIF_TIMER_UNITS_SET((uint32_t)cfg->verify_timer_units)
  );
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_fp_rmw_verify(
    core,
    &LAN9645_REG_SYS_FRONT_PORT_MODE,
    0u,
    (uint32_t)port,
    LAN9645_SYS_FRONT_PORT_MODE_ADD_FRAG_SIZE,
    LAN9645_SYS_FRONT_PORT_MODE_ADD_FRAG_SIZE_SET((uint32_t)cfg->add_frag_size)
  );
  if (st != APP_OK) {
    return st;
  }

  return lan9645_fp_rmw_verify(
    core,
    &LAN9645_REG_QSYS_PREEMPT_CFG,
    0u,
    (uint32_t)port,
    LAN9645_QSYS_PREEMPT_CFG_P_QUEUES |
      LAN9645_QSYS_PREEMPT_CFG_STRICT_IPG,
    LAN9645_QSYS_PREEMPT_CFG_P_QUEUES_SET((uint32_t)cfg->preempt_queue_mask) |
      LAN9645_QSYS_PREEMPT_CFG_STRICT_IPG_SET((uint32_t)cfg->strict_ipg)
  );
}

/**
 * @brief Applies FP baseline configuration on one managed port.
 */
static app_status_t lan9645_fp_apply_port_baseline(lan9645_core_t *core, uint8_t port)
{
  lan9645_fp_port_cfg_t cfg = {0};

  cfg.port = port;
  cfg.mm_rx_enable = 1u;
  cfg.mm_tx_enable = 1u;
  cfg.keep_s_after_d = 0u;
  cfg.verify_disable = 1u;
  cfg.verify_time = 1u;
  cfg.verify_timer_units = 0u;
  cfg.add_frag_size = 0u;
  cfg.preempt_queue_mask = 0u;
  cfg.strict_ipg = 0u;

  return lan9645_fp_set_port_cfg(core, port, &cfg);
}

/**
 * @brief Initializes FP baseline service state.
 */
app_status_t lan9645_fp_init(void)
{
  s_fp_initialized = 1u;
  return APP_OK;
}

/**
 * @brief Applies deterministic FP baseline for managed ports.
 */
app_status_t lan9645_fp_apply_baseline(lan9645_core_t *core)
{
  app_status_t st = lan9645_fp_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (s_fp_initialized == 0u) {
    (void)lan9645_fp_init();
  }

  for (uint32_t i = 0u; i < (uint32_t)(sizeof(s_fp_ports) / sizeof(s_fp_ports[0])); ++i) {
    st = lan9645_fp_apply_port_baseline(core, s_fp_ports[i]);
    if (st != APP_OK) {
      return st;
    }
  }

  return APP_OK;
}
