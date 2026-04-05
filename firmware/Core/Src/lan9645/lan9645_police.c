#include "lan9645/lan9645_police.h"

/**
 * @file lan9645_police.c
 * @brief Baseline policer-domain configuration aligned with Linux LAN9645 setup.
 */

#include "lan9645/lan9645_reg.h"
#include "lan9645/lan9645_regs.h"

#define LAN9645_POLICE_ORDER_VALUE         (0x1D3u)

static const uint8_t s_police_ports[] = {0u, 1u, 8u};
static uint8_t s_police_initialized = 0u;

/**
 * @brief Validates that the core/register layer is ready for police operations.
 */
static app_status_t lan9645_police_validate_core(const lan9645_core_t *core)
{
  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  return APP_OK;
}

/**
 * @brief Validates per-port policer configuration tuple before programming `ANA_POL_CFG`.
 */
static app_status_t lan9645_police_validate_port_cfg_values(
  uint8_t cpu_redir_8021,
  uint8_t cpu_redir_ip,
  uint8_t port_pol_enable,
  uint8_t queue_pol_enable,
  uint16_t pol_order
)
{
  if ((cpu_redir_8021 > 1u) ||
      (cpu_redir_ip > 1u) ||
      (port_pol_enable > 1u) ||
      (queue_pol_enable > LAN9645_POLICE_QUEUE_MASK_MAX) ||
      (pol_order > LAN9645_POLICE_ORDER_MAX)) {
    return APP_EINVAL;
  }
  return APP_OK;
}

/**
 * @brief Validates one policer index and full mode/rate tuple.
 */
static app_status_t lan9645_police_validate_index_cfg_values(
  uint8_t pol_idx,
  uint8_t drop_on_yellow,
  uint8_t mark_all_red,
  uint8_t ipg_size,
  uint8_t frm_mode,
  uint8_t dlb_coupled,
  uint8_t cir_enable,
  uint8_t overshoot_enable,
  uint32_t pir_level,
  uint16_t pir_rate,
  uint8_t pir_burst
)
{
  if (pol_idx >= LAN9645_POLICE_PORT_POL_COUNT) {
    return APP_EINVAL;
  }
  if ((drop_on_yellow > 1u) ||
      (mark_all_red > 1u) ||
      (ipg_size > LAN9645_POLICE_IPG_SIZE_MAX) ||
      (frm_mode > LAN9645_POLICE_FRM_MODE_MAX) ||
      (dlb_coupled > 1u) ||
      (cir_enable > 1u) ||
      (overshoot_enable > 1u) ||
      (pir_level > LAN9645_POLICE_PIR_LVL_MAX) ||
      (pir_rate > LAN9645_POLICE_PIR_RATE_MAX) ||
      (pir_burst > LAN9645_POLICE_PIR_BURST_MAX)) {
    return APP_EINVAL;
  }
  return APP_OK;
}

/**
 * @brief Performs a descriptor RMW and verifies masked readback.
 */
static app_status_t lan9645_police_rmw_verify(
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
 * @brief Writes one descriptor register and verifies masked readback.
 */
static app_status_t lan9645_police_write_verify(
  lan9645_core_t *core,
  const lan9645_reg_desc_t *desc,
  uint32_t group_instance,
  uint32_t reg_instance,
  uint32_t value,
  uint32_t mask
)
{
  uint32_t rd = 0u;
  app_status_t st = lan9645_reg_write32_desc(
    core->reg,
    desc,
    0u,
    group_instance,
    reg_instance,
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
 * @brief Reads one per-port policer configuration (`ANA_POL_CFG`).
 */
app_status_t lan9645_police_get_port_cfg(
  lan9645_core_t *core,
  uint8_t port,
  lan9645_police_port_cfg_t *out
)
{
  uint32_t rd = 0u;
  app_status_t st = lan9645_police_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (out == NULL) {
    return APP_EINVAL;
  }

  st = lan9645_reg_read32_desc(core->reg, &LAN9645_REG_ANA_POL_CFG, (uint32_t)port, 0u, 0u, &rd);
  if (st != APP_OK) {
    return st;
  }

  out->port = port;
  out->cpu_redir_8021 = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_POL_CFG_POL_CPU_REDIR_8021, rd);
  out->cpu_redir_ip = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_POL_CFG_POL_CPU_REDIR_IP, rd);
  out->port_pol_enable = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_POL_CFG_PORT_POL_ENA, rd);
  out->queue_pol_enable = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_POL_CFG_QUEUE_POL_ENA, rd);
  out->pol_order = (uint16_t)LAN9645_FIELD_GET(LAN9645_ANA_POL_CFG_POL_ORDER, rd);
  return APP_OK;
}

/**
 * @brief Writes one per-port policer configuration (`ANA_POL_CFG`) with readback verification.
 */
app_status_t lan9645_police_set_port_cfg(
  lan9645_core_t *core,
  uint8_t port,
  uint8_t cpu_redir_8021,
  uint8_t cpu_redir_ip,
  uint8_t port_pol_enable,
  uint8_t queue_pol_enable,
  uint16_t pol_order
)
{
  const uint32_t mask =
    LAN9645_ANA_POL_CFG_POL_CPU_REDIR_8021 |
    LAN9645_ANA_POL_CFG_POL_CPU_REDIR_IP |
    LAN9645_ANA_POL_CFG_PORT_POL_ENA |
    LAN9645_ANA_POL_CFG_QUEUE_POL_ENA |
    LAN9645_ANA_POL_CFG_POL_ORDER;
  app_status_t st = lan9645_police_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_police_validate_port_cfg_values(
    cpu_redir_8021,
    cpu_redir_ip,
    port_pol_enable,
    queue_pol_enable,
    pol_order
  );
  if (st != APP_OK) {
    return st;
  }

  return lan9645_police_rmw_verify(
    core,
    &LAN9645_REG_ANA_POL_CFG,
    (uint32_t)port,
    0u,
    mask,
    LAN9645_ANA_POL_CFG_POL_CPU_REDIR_8021_SET(cpu_redir_8021) |
      LAN9645_ANA_POL_CFG_POL_CPU_REDIR_IP_SET(cpu_redir_ip) |
      LAN9645_ANA_POL_CFG_PORT_POL_ENA_SET(port_pol_enable) |
      LAN9645_ANA_POL_CFG_QUEUE_POL_ENA_SET(queue_pol_enable) |
      LAN9645_ANA_POL_CFG_POL_ORDER_SET(pol_order)
  );
}

/**
 * @brief Reads one policer index configuration (`ANA_POL_MODE/PIR_*`).
 */
app_status_t lan9645_police_get_index_cfg(
  lan9645_core_t *core,
  uint8_t pol_idx,
  lan9645_police_index_cfg_t *out
)
{
  uint32_t mode = 0u;
  uint32_t state = 0u;
  uint32_t pir = 0u;
  app_status_t st = lan9645_police_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (out == NULL) {
    return APP_EINVAL;
  }
  if (pol_idx >= LAN9645_POLICE_PORT_POL_COUNT) {
    return APP_EINVAL;
  }

  st = lan9645_reg_read32_desc(core->reg, &LAN9645_REG_ANA_POL_MODE, pol_idx, 0u, 0u, &mode);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_reg_read32_desc(core->reg, &LAN9645_REG_ANA_POL_PIR_STATE, pol_idx, 0u, 0u, &state);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_reg_read32_desc(core->reg, &LAN9645_REG_ANA_POL_PIR_CFG, pol_idx, 0u, 0u, &pir);
  if (st != APP_OK) {
    return st;
  }

  out->pol_idx = pol_idx;
  out->drop_on_yellow = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_POL_MODE_DROP_ON_YELLOW_ENA, mode);
  out->mark_all_red = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_POL_MODE_MARK_ALL_FRMS_RED_ENA, mode);
  out->ipg_size = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_POL_MODE_IPG_SIZE, mode);
  out->frm_mode = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_POL_MODE_FRM_MODE, mode);
  out->dlb_coupled = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_POL_MODE_DLB_COUPLED, mode);
  out->cir_enable = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_POL_MODE_CIR_ENA, mode);
  out->overshoot_enable = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_POL_MODE_OVERSHOOT_ENA, mode);
  out->pir_level = LAN9645_FIELD_GET(LAN9645_ANA_POL_PIR_STATE_PIR_LVL, state);
  out->pir_rate = (uint16_t)LAN9645_FIELD_GET(LAN9645_ANA_POL_PIR_CFG_PIR_RATE, pir);
  out->pir_burst = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_POL_PIR_CFG_PIR_BURST, pir);
  return APP_OK;
}

/**
 * @brief Writes one policer index configuration (`ANA_POL_MODE/PIR_*`) with readback verification.
 */
app_status_t lan9645_police_set_index_cfg(
  lan9645_core_t *core,
  uint8_t pol_idx,
  uint8_t drop_on_yellow,
  uint8_t mark_all_red,
  uint8_t ipg_size,
  uint8_t frm_mode,
  uint8_t dlb_coupled,
  uint8_t cir_enable,
  uint8_t overshoot_enable,
  uint32_t pir_level,
  uint16_t pir_rate,
  uint8_t pir_burst
)
{
  const uint32_t mode_mask =
    LAN9645_ANA_POL_MODE_DROP_ON_YELLOW_ENA |
    LAN9645_ANA_POL_MODE_MARK_ALL_FRMS_RED_ENA |
    LAN9645_ANA_POL_MODE_IPG_SIZE |
    LAN9645_ANA_POL_MODE_FRM_MODE |
    LAN9645_ANA_POL_MODE_DLB_COUPLED |
    LAN9645_ANA_POL_MODE_CIR_ENA |
    LAN9645_ANA_POL_MODE_OVERSHOOT_ENA;
  app_status_t st = lan9645_police_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_police_validate_index_cfg_values(
    pol_idx,
    drop_on_yellow,
    mark_all_red,
    ipg_size,
    frm_mode,
    dlb_coupled,
    cir_enable,
    overshoot_enable,
    pir_level,
    pir_rate,
    pir_burst
  );
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_police_write_verify(
    core,
    &LAN9645_REG_ANA_POL_MODE,
    pol_idx,
    0u,
    LAN9645_ANA_POL_MODE_DROP_ON_YELLOW_ENA_SET(drop_on_yellow) |
      LAN9645_ANA_POL_MODE_MARK_ALL_FRMS_RED_ENA_SET(mark_all_red) |
      LAN9645_ANA_POL_MODE_IPG_SIZE_SET(ipg_size) |
      LAN9645_ANA_POL_MODE_FRM_MODE_SET(frm_mode) |
      LAN9645_ANA_POL_MODE_DLB_COUPLED_SET(dlb_coupled) |
      LAN9645_ANA_POL_MODE_CIR_ENA_SET(cir_enable) |
      LAN9645_ANA_POL_MODE_OVERSHOOT_ENA_SET(overshoot_enable),
    mode_mask
  );
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_police_write_verify(
    core,
    &LAN9645_REG_ANA_POL_PIR_STATE,
    pol_idx,
    0u,
    LAN9645_ANA_POL_PIR_STATE_PIR_LVL_SET(pir_level),
    LAN9645_ANA_POL_PIR_STATE_PIR_LVL
  );
  if (st != APP_OK) {
    return st;
  }

  return lan9645_police_write_verify(
    core,
    &LAN9645_REG_ANA_POL_PIR_CFG,
    pol_idx,
    0u,
    LAN9645_ANA_POL_PIR_CFG_PIR_RATE_SET(pir_rate) |
      LAN9645_ANA_POL_PIR_CFG_PIR_BURST_SET(pir_burst),
    LAN9645_ANA_POL_PIR_CFG_PIR_RATE |
      LAN9645_ANA_POL_PIR_CFG_PIR_BURST
  );
}

/**
 * @brief Initializes policer baseline service state.
 */
app_status_t lan9645_police_init(void)
{
  s_police_initialized = 1u;
  return APP_OK;
}

/**
 * @brief Applies deterministic policer baseline for managed mode.
 */
app_status_t lan9645_police_apply_baseline(lan9645_core_t *core)
{
  app_status_t st = lan9645_police_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (s_police_initialized == 0u) {
    (void)lan9645_police_init();
  }

  for (uint32_t i = 0u; i < (uint32_t)(sizeof(s_police_ports) / sizeof(s_police_ports[0])); ++i) {
    st = lan9645_police_set_port_cfg(
      core,
      s_police_ports[i],
      1u,
      1u,
      0u,
      0u,
      LAN9645_POLICE_ORDER_VALUE
    );
    if (st != APP_OK) {
      return st;
    }
  }

  for (uint32_t pol_idx = 0u; pol_idx < LAN9645_POLICE_PORT_POL_COUNT; ++pol_idx) {
    st = lan9645_police_set_index_cfg(
      core,
      (uint8_t)pol_idx,
      0u,
      0u,
      20u,
      2u,
      0u,
      0u,
      1u,
      0u,
      LAN9645_POLICE_PIR_RATE_MAX,
      0u
    );
    if (st != APP_OK) {
      return st;
    }
  }

  return APP_OK;
}
