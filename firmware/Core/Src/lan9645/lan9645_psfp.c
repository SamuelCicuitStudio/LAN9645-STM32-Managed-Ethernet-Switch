#include "lan9645/lan9645_psfp.h"

/**
 * @file lan9645_psfp.c
 * @brief Baseline PSFP reset path aligned with Linux LAN9645 stream-gate/filter flow.
 */

#include "lan9645/lan9645_reg.h"
#include "lan9645/lan9645_regs.h"

#define LAN9645_PSFP_SFID_CMD_IDLE              (0u)
#define LAN9645_PSFP_SFID_CMD_WRITE             (2u)
#define LAN9645_PSFP_TIMEOUT_MS                 (50u)
#define LAN9645_PSFP_BASE_INDEX                 (0u)
#define LAN9645_PSFP_GCL_ENTRIES                (4u)

static uint8_t s_psfp_initialized = 0u;

/**
 * @brief Validates that PSFP operations can access an initialized register layer.
 */
static app_status_t lan9645_psfp_validate_core(const lan9645_core_t *core)
{
  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  return APP_OK;
}

/**
 * @brief Validates SFID tuple values before SFID table operations.
 */
static app_status_t lan9645_psfp_validate_sfid_values(
  uint8_t sfid_index,
  uint8_t bo_frm,
  uint8_t bo_frm_enable,
  uint8_t force_block,
  uint16_t max_sdu_len
)
{
  if ((sfid_index > LAN9645_PSFP_INDEX_MAX) ||
      (bo_frm > 1u) ||
      (bo_frm_enable > 1u) ||
      (force_block > 1u) ||
      (max_sdu_len > LAN9645_PSFP_MAX_SDU_LEN_MAX)) {
    return APP_EINVAL;
  }
  return APP_OK;
}

/**
 * @brief Validates SG gate-configuration tuple values before programming `ANA_SG_CFG_3`.
 */
static app_status_t lan9645_psfp_validate_gate_values(
  uint8_t sgid,
  uint8_t list_length,
  uint8_t gate_enable,
  uint8_t init_ips,
  uint8_t init_gate_state
)
{
  if ((sgid > LAN9645_PSFP_INDEX_MAX) ||
      (list_length > LAN9645_PSFP_LIST_LENGTH_MAX) ||
      (gate_enable > 1u) ||
      (init_ips > LAN9645_PSFP_INIT_IPS_MAX) ||
      (init_gate_state > 1u)) {
    return APP_EINVAL;
  }
  return APP_OK;
}

/**
 * @brief Writes one descriptor register and verifies masked readback.
 */
static app_status_t lan9645_psfp_write_verify(
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
 * @brief Waits for SFID table command completion.
 */
static app_status_t lan9645_psfp_wait_sfid_idle(lan9645_core_t *core)
{
  return lan9645_reg_wait32_desc(
    core->reg,
    &LAN9645_REG_ANA_SFIDACCESS,
    0u,
    0u,
    0u,
    LAN9645_ANA_SFIDACCESS_SFID_TBL_CMD,
    LAN9645_ANA_SFIDACCESS_SFID_TBL_CMD_SET(LAN9645_PSFP_SFID_CMD_IDLE),
    LAN9645_PSFP_TIMEOUT_MS,
    NULL
  );
}

/**
 * @brief Waits for SG configuration change handshake completion.
 */
static app_status_t lan9645_psfp_wait_sg_cfg_done(lan9645_core_t *core)
{
  return lan9645_reg_wait32_desc(
    core->reg,
    &LAN9645_REG_ANA_SG_ACCESS_CTRL,
    0u,
    0u,
    0u,
    LAN9645_ANA_SG_ACCESS_CTRL_CONFIG_CHANGE,
    LAN9645_ANA_SG_ACCESS_CTRL_CONFIG_CHANGE_SET(0u),
    LAN9645_PSFP_TIMEOUT_MS,
    NULL
  );
}

/**
 * @brief Selects one SFID table index through `ANA_SFIDTIDX`.
 */
static app_status_t lan9645_psfp_select_sfid(lan9645_core_t *core, uint8_t sfid_index)
{
  return lan9645_psfp_write_verify(
    core,
    &LAN9645_REG_ANA_SFIDTIDX,
    0u,
    0u,
    LAN9645_ANA_SFIDTIDX_SFID_INDEX_SET(sfid_index),
    LAN9645_ANA_SFIDTIDX_SFID_INDEX
  );
}

/**
 * @brief Selects one SG index through `ANA_SG_ACCESS_CTRL` (without commit).
 */
static app_status_t lan9645_psfp_select_sgid(lan9645_core_t *core, uint8_t sgid)
{
  const uint32_t sg_access_select_mask =
    LAN9645_ANA_SG_ACCESS_CTRL_SGID |
    LAN9645_ANA_SG_ACCESS_CTRL_CONFIG_CHANGE;
  return lan9645_psfp_write_verify(
    core,
    &LAN9645_REG_ANA_SG_ACCESS_CTRL,
    0u,
    0u,
    LAN9645_ANA_SG_ACCESS_CTRL_SGID_SET(sgid),
    sg_access_select_mask
  );
}

/**
 * @brief Initializes PSFP baseline service state.
 */
app_status_t lan9645_psfp_init(void)
{
  s_psfp_initialized = 1u;
  return APP_OK;
}

/**
 * @brief Reads one PSFP SFID entry snapshot.
 */
app_status_t lan9645_psfp_get_sfid_cfg(
  lan9645_core_t *core,
  uint8_t sfid_index,
  lan9645_psfp_sfid_cfg_t *out
)
{
  uint32_t rd = 0u;
  app_status_t st = lan9645_psfp_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (out == NULL) {
    return APP_EINVAL;
  }
  st = lan9645_psfp_validate_sfid_values(sfid_index, 0u, 0u, 0u, 0u);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_psfp_select_sfid(core, sfid_index);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_reg_read32_desc(core->reg, &LAN9645_REG_ANA_SFIDACCESS, 0u, 0u, 0u, &rd);
  if (st != APP_OK) {
    return st;
  }

  out->sfid_index = sfid_index;
  out->bo_frm = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_SFIDACCESS_B_O_FRM, rd);
  out->bo_frm_enable = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_SFIDACCESS_B_O_FRM_ENA, rd);
  out->force_block = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_SFIDACCESS_FORCE_BLOCK, rd);
  out->max_sdu_len = (uint16_t)LAN9645_FIELD_GET(LAN9645_ANA_SFIDACCESS_MAX_SDU_LEN, rd);
  return APP_OK;
}

/**
 * @brief Writes one PSFP SFID entry and waits for SFID table command completion.
 */
app_status_t lan9645_psfp_set_sfid_cfg(
  lan9645_core_t *core,
  uint8_t sfid_index,
  uint8_t bo_frm,
  uint8_t bo_frm_enable,
  uint8_t force_block,
  uint16_t max_sdu_len
)
{
  const uint32_t sfid_cmd_mask =
    LAN9645_ANA_SFIDACCESS_B_O_FRM |
    LAN9645_ANA_SFIDACCESS_B_O_FRM_ENA |
    LAN9645_ANA_SFIDACCESS_FORCE_BLOCK |
    LAN9645_ANA_SFIDACCESS_MAX_SDU_LEN |
    LAN9645_ANA_SFIDACCESS_SFID_TBL_CMD;
  app_status_t st = lan9645_psfp_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_psfp_validate_sfid_values(
    sfid_index,
    bo_frm,
    bo_frm_enable,
    force_block,
    max_sdu_len
  );
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_psfp_select_sfid(core, sfid_index);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_psfp_write_verify(
    core,
    &LAN9645_REG_ANA_SFIDACCESS,
    0u,
    0u,
    LAN9645_ANA_SFIDACCESS_B_O_FRM_SET(bo_frm) |
      LAN9645_ANA_SFIDACCESS_B_O_FRM_ENA_SET(bo_frm_enable) |
      LAN9645_ANA_SFIDACCESS_FORCE_BLOCK_SET(force_block) |
      LAN9645_ANA_SFIDACCESS_MAX_SDU_LEN_SET(max_sdu_len) |
      LAN9645_ANA_SFIDACCESS_SFID_TBL_CMD_SET(LAN9645_PSFP_SFID_CMD_WRITE),
    sfid_cmd_mask
  );
  if (st != APP_OK) {
    return st;
  }

  return lan9645_psfp_wait_sfid_idle(core);
}

/**
 * @brief Reads one PSFP stream-gate base configuration (`ANA_SG_CFG_3`).
 */
app_status_t lan9645_psfp_get_gate_cfg(
  lan9645_core_t *core,
  uint8_t sgid,
  lan9645_psfp_gate_cfg_t *out
)
{
  uint32_t rd = 0u;
  app_status_t st = lan9645_psfp_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (out == NULL) {
    return APP_EINVAL;
  }
  st = lan9645_psfp_validate_gate_values(sgid, 0u, 0u, 0u, 0u);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_psfp_select_sgid(core, sgid);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_reg_read32_desc(core->reg, &LAN9645_REG_ANA_SG_CFG_3, 0u, 0u, 0u, &rd);
  if (st != APP_OK) {
    return st;
  }

  out->sgid = sgid;
  out->list_length = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_SG_CFG_3_LIST_LENGTH, rd);
  out->gate_enable = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_SG_CFG_3_GATE_ENABLE, rd);
  out->init_ips = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_SG_CFG_3_INIT_IPS, rd);
  out->init_gate_state = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_SG_CFG_3_INIT_GATE_STATE, rd);
  return APP_OK;
}

/**
 * @brief Writes one PSFP stream-gate base configuration (`ANA_SG_CFG_3`).
 */
app_status_t lan9645_psfp_set_gate_cfg(
  lan9645_core_t *core,
  uint8_t sgid,
  uint8_t list_length,
  uint8_t gate_enable,
  uint8_t init_ips,
  uint8_t init_gate_state
)
{
  const uint32_t sg_cfg3_mask =
    LAN9645_ANA_SG_CFG_3_LIST_LENGTH |
    LAN9645_ANA_SG_CFG_3_GATE_ENABLE |
    LAN9645_ANA_SG_CFG_3_INIT_IPS |
    LAN9645_ANA_SG_CFG_3_INIT_GATE_STATE;
  app_status_t st = lan9645_psfp_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_psfp_validate_gate_values(
    sgid,
    list_length,
    gate_enable,
    init_ips,
    init_gate_state
  );
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_psfp_select_sgid(core, sgid);
  if (st != APP_OK) {
    return st;
  }

  return lan9645_psfp_write_verify(
    core,
    &LAN9645_REG_ANA_SG_CFG_3,
    0u,
    0u,
    LAN9645_ANA_SG_CFG_3_LIST_LENGTH_SET(list_length) |
      LAN9645_ANA_SG_CFG_3_GATE_ENABLE_SET(gate_enable) |
      LAN9645_ANA_SG_CFG_3_INIT_IPS_SET(init_ips) |
      LAN9645_ANA_SG_CFG_3_INIT_GATE_STATE_SET(init_gate_state),
    sg_cfg3_mask
  );
}

/**
 * @brief Commits one PSFP stream-gate configuration and waits for completion.
 */
app_status_t lan9645_psfp_commit_gate_cfg(lan9645_core_t *core, uint8_t sgid)
{
  app_status_t st = lan9645_psfp_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (sgid > LAN9645_PSFP_INDEX_MAX) {
    return APP_EINVAL;
  }

  st = lan9645_reg_write32_desc(
    core->reg,
    &LAN9645_REG_ANA_SG_ACCESS_CTRL,
    0u,
    0u,
    0u,
    LAN9645_ANA_SG_ACCESS_CTRL_SGID_SET(sgid) |
      LAN9645_ANA_SG_ACCESS_CTRL_CONFIG_CHANGE_SET(1u)
  );
  if (st != APP_OK) {
    return st;
  }

  return lan9645_psfp_wait_sg_cfg_done(core);
}

/**
 * @brief Applies deterministic PSFP baseline reset on stream filter/gate index 0.
 */
app_status_t lan9645_psfp_apply_baseline(lan9645_core_t *core)
{
  const uint32_t sg_gcl_gs_mask =
    LAN9645_ANA_SG_GCL_GS_CFG_IPS |
    LAN9645_ANA_SG_GCL_GS_CFG_GATE_STATE;
  app_status_t st = lan9645_psfp_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (s_psfp_initialized == 0u) {
    (void)lan9645_psfp_init();
  }

  st = lan9645_psfp_set_sfid_cfg(core, LAN9645_PSFP_BASE_INDEX, 0u, 0u, 0u, 0u);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_psfp_set_gate_cfg(core, LAN9645_PSFP_BASE_INDEX, 0u, 0u, 0u, 1u);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_psfp_select_sgid(core, LAN9645_PSFP_BASE_INDEX);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_psfp_write_verify(core, &LAN9645_REG_ANA_SG_CFG_1, 0u, 0u, 0u, 0xFFFFFFFFu);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_psfp_write_verify(core, &LAN9645_REG_ANA_SG_CFG_2, 0u, 0u, 0u, 0xFFFFFFFFu);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_psfp_write_verify(core, &LAN9645_REG_ANA_SG_CFG_4, 0u, 0u, 0u, 0xFFFFFFFFu);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_psfp_write_verify(core, &LAN9645_REG_ANA_SG_CFG_5, 0u, 0u, 0u, 0xFFFFFFFFu);
  if (st != APP_OK) {
    return st;
  }

  for (uint32_t i = 0u; i < LAN9645_PSFP_GCL_ENTRIES; ++i) {
    st = lan9645_psfp_write_verify(core, &LAN9645_REG_ANA_SG_GCL_GS_CFG, 0u, i, 0u, sg_gcl_gs_mask);
    if (st != APP_OK) {
      return st;
    }
    st = lan9645_psfp_write_verify(core, &LAN9645_REG_ANA_SG_GCL_TI_CFG, 0u, i, 0u, 0xFFFFFFFFu);
    if (st != APP_OK) {
      return st;
    }
    st = lan9645_psfp_write_verify(core, &LAN9645_REG_ANA_SG_GCL_OCT_CFG, 0u, i, 0u, 0xFFFFFFFFu);
    if (st != APP_OK) {
      return st;
    }
  }

  return lan9645_psfp_commit_gate_cfg(core, LAN9645_PSFP_BASE_INDEX);
}
