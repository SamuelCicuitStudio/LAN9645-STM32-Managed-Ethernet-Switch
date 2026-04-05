#include "lan9645/lan9645_taprio.h"

/**
 * @file lan9645_taprio.c
 * @brief Baseline TAPRIO/TAS configuration aligned with Linux LAN9645 managed behavior.
 */

#include "lan9645/lan9645_reg.h"
#include "lan9645/lan9645_regs.h"

#define LAN9645_TAPRIO_TAS_STATE_ADMIN        (0u)
#define LAN9645_TAPRIO_REVISIT_DLY_DEFAULT    (32u)
#define LAN9645_TAPRIO_TAS_ENTRIES_PER_PORT   (2u)
#define LAN9645_TAPRIO_GATE_ALL_OPEN          (0xFFu)
#define LAN9645_TAPRIO_TIMEOUT_MS             (100u)

static const uint8_t s_taprio_ports[] = {0u, 1u, 8u};
static uint8_t s_taprio_initialized = 0u;

/**
 * @brief Validates that TAPRIO operations can access an initialized register layer.
 */
static app_status_t lan9645_taprio_validate_core(const lan9645_core_t *core)
{
  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  return APP_OK;
}

/**
 * @brief Validates global TAS/TAPRIO configuration tuple before register programming.
 */
static app_status_t lan9645_taprio_validate_cfg_values(
  uint8_t revisit_dly,
  uint8_t list_num_max,
  uint8_t always_guard_band
)
{
  if ((revisit_dly > LAN9645_TAPRIO_REVISIT_DLY_MAX) ||
      (list_num_max > LAN9645_TAPRIO_LIST_NUM_MAX_MAX) ||
      (always_guard_band > 1u)) {
    return APP_EINVAL;
  }
  return APP_OK;
}

/**
 * @brief Validates list selector/state values before TAS list-state access.
 */
static app_status_t lan9645_taprio_validate_list_values(uint8_t list_num, uint8_t list_state)
{
  if ((list_num > LAN9645_TAPRIO_LIST_NUM_MAX) ||
      (list_state > LAN9645_TAPRIO_LIST_STATE_MAX)) {
    return APP_EINVAL;
  }
  return APP_OK;
}

/**
 * @brief Validates one gate-state mask value.
 */
static app_status_t lan9645_taprio_validate_gate_mask(uint8_t gate_mask)
{
  if (gate_mask > LAN9645_TAPRIO_GATE_MASK_MAX) {
    return APP_EINVAL;
  }
  return APP_OK;
}

/**
 * @brief Performs a descriptor RMW and verifies masked readback.
 */
static app_status_t lan9645_taprio_rmw_verify(
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
static app_status_t lan9645_taprio_write_verify(
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
 * @brief Selects one TAS list index through `TAS_CFG_CTRL.LIST_NUM`.
 */
static app_status_t lan9645_taprio_select_list(lan9645_core_t *core, uint8_t list_num)
{
  return lan9645_taprio_rmw_verify(
    core,
    &LAN9645_REG_TAS_TAS_CFG_CTRL,
    0u,
    0u,
    LAN9645_TAS_TAS_CFG_CTRL_LIST_NUM,
    LAN9645_TAS_TAS_CFG_CTRL_LIST_NUM_SET(list_num)
  );
}

/**
 * @brief Selects one TAS gate-state port position through `TAS_GATE_STATE_CTRL.HSCH_POS`.
 */
static app_status_t lan9645_taprio_select_gate_port(lan9645_core_t *core, uint8_t port)
{
  return lan9645_taprio_rmw_verify(
    core,
    &LAN9645_REG_TAS_TAS_GATE_STATE_CTRL,
    0u,
    0u,
    LAN9645_TAS_TAS_GATE_STATE_CTRL_HSCH_POS,
    LAN9645_TAS_TAS_GATE_STATE_CTRL_HSCH_POS_SET(port)
  );
}

/**
 * @brief Reads global TAS/TAPRIO scheduler configuration.
 */
app_status_t lan9645_taprio_get_cfg(lan9645_core_t *core, lan9645_taprio_cfg_t *out)
{
  uint32_t cfg_ctrl = 0u;
  uint32_t sm_cfg = 0u;
  app_status_t st = lan9645_taprio_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (out == NULL) {
    return APP_EINVAL;
  }

  st = lan9645_reg_read32_desc(core->reg, &LAN9645_REG_TAS_TAS_STATEMACHINE_CFG, 0u, 0u, 0u, &sm_cfg);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_reg_read32_desc(core->reg, &LAN9645_REG_TAS_TAS_CFG_CTRL, 0u, 0u, 0u, &cfg_ctrl);
  if (st != APP_OK) {
    return st;
  }

  out->revisit_dly = (uint8_t)LAN9645_FIELD_GET(LAN9645_TAS_TAS_STATEMACHINE_CFG_REVISIT_DLY, sm_cfg);
  out->list_num_max = (uint8_t)LAN9645_FIELD_GET(LAN9645_TAS_TAS_CFG_CTRL_LIST_NUM_MAX, cfg_ctrl);
  out->list_num_selected = (uint8_t)LAN9645_FIELD_GET(LAN9645_TAS_TAS_CFG_CTRL_LIST_NUM, cfg_ctrl);
  out->always_guard_band = (uint8_t)LAN9645_FIELD_GET(LAN9645_TAS_TAS_CFG_CTRL_ALWAYS_GUARD_BAND_SCH_Q, cfg_ctrl);
  return APP_OK;
}

/**
 * @brief Writes global TAS/TAPRIO scheduler configuration with readback verification.
 */
app_status_t lan9645_taprio_set_cfg(
  lan9645_core_t *core,
  uint8_t revisit_dly,
  uint8_t list_num_max,
  uint8_t always_guard_band
)
{
  app_status_t st = lan9645_taprio_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_taprio_validate_cfg_values(revisit_dly, list_num_max, always_guard_band);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_taprio_rmw_verify(
    core,
    &LAN9645_REG_TAS_TAS_STATEMACHINE_CFG,
    0u,
    0u,
    LAN9645_TAS_TAS_STATEMACHINE_CFG_REVISIT_DLY,
    LAN9645_TAS_TAS_STATEMACHINE_CFG_REVISIT_DLY_SET(revisit_dly)
  );
  if (st != APP_OK) {
    return st;
  }

  return lan9645_taprio_rmw_verify(
    core,
    &LAN9645_REG_TAS_TAS_CFG_CTRL,
    0u,
    0u,
    LAN9645_TAS_TAS_CFG_CTRL_LIST_NUM_MAX |
      LAN9645_TAS_TAS_CFG_CTRL_ALWAYS_GUARD_BAND_SCH_Q,
    LAN9645_TAS_TAS_CFG_CTRL_LIST_NUM_MAX_SET(list_num_max) |
      LAN9645_TAS_TAS_CFG_CTRL_ALWAYS_GUARD_BAND_SCH_Q_SET(always_guard_band)
  );
}

/**
 * @brief Reads one TAS list state after selecting list index.
 */
app_status_t lan9645_taprio_get_list_state(
  lan9645_core_t *core,
  uint8_t list_num,
  uint8_t *out_list_state
)
{
  uint32_t rd = 0u;
  app_status_t st = lan9645_taprio_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (out_list_state == NULL) {
    return APP_EINVAL;
  }
  st = lan9645_taprio_validate_list_values(list_num, 0u);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_taprio_select_list(core, list_num);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_reg_read32_desc(core->reg, &LAN9645_REG_TAS_TAS_LIST_STATE, 0u, 0u, 0u, &rd);
  if (st != APP_OK) {
    return st;
  }
  *out_list_state = (uint8_t)LAN9645_FIELD_GET(LAN9645_TAS_TAS_LIST_STATE_LIST_STATE, rd);
  return APP_OK;
}

/**
 * @brief Writes one TAS list state after selecting list index and verifies readback.
 */
app_status_t lan9645_taprio_set_list_state(
  lan9645_core_t *core,
  uint8_t list_num,
  uint8_t list_state
)
{
  app_status_t st = lan9645_taprio_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_taprio_validate_list_values(list_num, list_state);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_taprio_select_list(core, list_num);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_taprio_rmw_verify(
    core,
    &LAN9645_REG_TAS_TAS_LIST_STATE,
    0u,
    0u,
    LAN9645_TAS_TAS_LIST_STATE_LIST_STATE,
    LAN9645_TAS_TAS_LIST_STATE_LIST_STATE_SET(list_state)
  );
  if (st != APP_OK) {
    return st;
  }

  return lan9645_reg_wait32_desc(
    core->reg,
    &LAN9645_REG_TAS_TAS_LIST_STATE,
    0u,
    0u,
    0u,
    LAN9645_TAS_TAS_LIST_STATE_LIST_STATE,
    LAN9645_TAS_TAS_LIST_STATE_LIST_STATE_SET(list_state),
    LAN9645_TAPRIO_TIMEOUT_MS,
    NULL
  );
}

/**
 * @brief Reads per-port TAS gate-state mask.
 */
app_status_t lan9645_taprio_get_port_gate_state(
  lan9645_core_t *core,
  uint8_t port,
  uint8_t *out_gate_mask
)
{
  uint32_t rd = 0u;
  app_status_t st = lan9645_taprio_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (out_gate_mask == NULL) {
    return APP_EINVAL;
  }

  st = lan9645_taprio_select_gate_port(core, port);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_reg_read32_desc(core->reg, &LAN9645_REG_TAS_TAS_GATE_STATE, 0u, 0u, 0u, &rd);
  if (st != APP_OK) {
    return st;
  }
  *out_gate_mask = (uint8_t)LAN9645_FIELD_GET(LAN9645_TAS_TAS_GATE_STATE_TAS_GATE_STATE, rd);
  return APP_OK;
}

/**
 * @brief Writes per-port TAS gate-state mask and verifies readback.
 */
app_status_t lan9645_taprio_set_port_gate_state(
  lan9645_core_t *core,
  uint8_t port,
  uint8_t gate_mask
)
{
  app_status_t st = lan9645_taprio_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_taprio_validate_gate_mask(gate_mask);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_taprio_select_gate_port(core, port);
  if (st != APP_OK) {
    return st;
  }

  return lan9645_taprio_write_verify(
    core,
    &LAN9645_REG_TAS_TAS_GATE_STATE,
    0u,
    0u,
    LAN9645_TAS_TAS_GATE_STATE_TAS_GATE_STATE_SET(gate_mask),
    LAN9645_TAS_TAS_GATE_STATE_TAS_GATE_STATE
  );
}

/**
 * @brief Initializes TAPRIO baseline service state.
 */
app_status_t lan9645_taprio_init(void)
{
  s_taprio_initialized = 1u;
  return APP_OK;
}

/**
 * @brief Applies deterministic TAPRIO baseline for managed mode.
 */
app_status_t lan9645_taprio_apply_baseline(lan9645_core_t *core)
{
  const uint32_t managed_port_count = (uint32_t)(sizeof(s_taprio_ports) / sizeof(s_taprio_ports[0]));
  const uint32_t tas_list_count = managed_port_count * LAN9645_TAPRIO_TAS_ENTRIES_PER_PORT;
  app_status_t st = lan9645_taprio_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (s_taprio_initialized == 0u) {
    (void)lan9645_taprio_init();
  }

  st = lan9645_taprio_set_cfg(
    core,
    LAN9645_TAPRIO_REVISIT_DLY_DEFAULT,
    (uint8_t)tas_list_count,
    1u
  );
  if (st != APP_OK) {
    return st;
  }

  for (uint32_t list_idx = 0u; list_idx < tas_list_count; ++list_idx) {
    st = lan9645_taprio_set_list_state(core, (uint8_t)list_idx, LAN9645_TAPRIO_TAS_STATE_ADMIN);
    if (st != APP_OK) {
      return st;
    }
  }

  for (uint32_t i = 0u; i < managed_port_count; ++i) {
    st = lan9645_taprio_set_port_gate_state(core, s_taprio_ports[i], LAN9645_TAPRIO_GATE_ALL_OPEN);
    if (st != APP_OK) {
      return st;
    }
  }

  return APP_OK;
}
