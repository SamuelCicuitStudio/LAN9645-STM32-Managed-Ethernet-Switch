#include "lan9645/lan9645_stream.h"

/**
 * @file lan9645_stream.c
 * @brief Baseline stream-domain reset path aligned with LAN9645 Linux reference flow.
 */

#include "lan9645/lan9645_reg.h"
#include "lan9645/lan9645_regs.h"

#define LAN9645_STREAM_SFID_CMD_IDLE   (0u)
#define LAN9645_STREAM_SFID_CMD_WRITE  (2u)
#define LAN9645_STREAM_TIMEOUT_MS      (50u)
#define LAN9645_STREAM_BASE_INDEX      (0u)

static uint8_t s_stream_initialized = 0u;

/**
 * @brief Validates that stream operations can access an initialized register layer.
 */
static app_status_t lan9645_stream_validate_core(const lan9645_core_t *core)
{
  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  return APP_OK;
}

/**
 * @brief Validates SFID index and field tuple before stream table operations.
 */
static app_status_t lan9645_stream_validate_sfid_values(
  uint8_t sfid_index,
  uint8_t bo_frm,
  uint8_t bo_frm_enable,
  uint8_t force_block,
  uint16_t max_sdu_len
)
{
  if ((sfid_index > LAN9645_STREAM_SFID_INDEX_MAX) ||
      (bo_frm > 1u) ||
      (bo_frm_enable > 1u) ||
      (force_block > 1u) ||
      (max_sdu_len > LAN9645_STREAM_MAX_SDU_LEN_MAX)) {
    return APP_EINVAL;
  }
  return APP_OK;
}

/**
 * @brief Writes one descriptor register and verifies masked readback.
 */
static app_status_t lan9645_stream_write_verify(
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
static app_status_t lan9645_stream_wait_sfid_idle(lan9645_core_t *core)
{
  return lan9645_reg_wait32_desc(
    core->reg,
    &LAN9645_REG_ANA_SFIDACCESS,
    0u,
    0u,
    0u,
    LAN9645_ANA_SFIDACCESS_SFID_TBL_CMD,
    LAN9645_ANA_SFIDACCESS_SFID_TBL_CMD_SET(LAN9645_STREAM_SFID_CMD_IDLE),
    LAN9645_STREAM_TIMEOUT_MS,
    NULL
  );
}

/**
 * @brief Selects one SFID table index.
 */
static app_status_t lan9645_stream_select_sfid(lan9645_core_t *core, uint8_t sfid_index)
{
  return lan9645_stream_write_verify(
    core,
    &LAN9645_REG_ANA_SFIDTIDX,
    0u,
    0u,
    LAN9645_ANA_SFIDTIDX_SFID_INDEX_SET(sfid_index),
    LAN9645_ANA_SFIDTIDX_SFID_INDEX
  );
}

/**
 * @brief Initializes stream baseline service state.
 */
app_status_t lan9645_stream_init(void)
{
  s_stream_initialized = 1u;
  return APP_OK;
}

/**
 * @brief Reads one SFID entry snapshot.
 */
app_status_t lan9645_stream_get_sfid_cfg(
  lan9645_core_t *core,
  uint8_t sfid_index,
  lan9645_stream_sfid_cfg_t *out
)
{
  uint32_t rd = 0u;
  app_status_t st = lan9645_stream_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (out == NULL) {
    return APP_EINVAL;
  }
  st = lan9645_stream_validate_sfid_values(sfid_index, 0u, 0u, 0u, 0u);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_stream_select_sfid(core, sfid_index);
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
 * @brief Writes one SFID entry and waits for SFID command completion.
 */
app_status_t lan9645_stream_set_sfid_cfg(
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
  app_status_t st = lan9645_stream_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_stream_validate_sfid_values(
    sfid_index,
    bo_frm,
    bo_frm_enable,
    force_block,
    max_sdu_len
  );
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_stream_select_sfid(core, sfid_index);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_stream_write_verify(
    core,
    &LAN9645_REG_ANA_SFIDACCESS,
    0u,
    0u,
    LAN9645_ANA_SFIDACCESS_B_O_FRM_SET(bo_frm) |
      LAN9645_ANA_SFIDACCESS_B_O_FRM_ENA_SET(bo_frm_enable) |
      LAN9645_ANA_SFIDACCESS_FORCE_BLOCK_SET(force_block) |
      LAN9645_ANA_SFIDACCESS_MAX_SDU_LEN_SET(max_sdu_len) |
      LAN9645_ANA_SFIDACCESS_SFID_TBL_CMD_SET(LAN9645_STREAM_SFID_CMD_WRITE),
    sfid_cmd_mask
  );
  if (st != APP_OK) {
    return st;
  }

  return lan9645_stream_wait_sfid_idle(core);
}

/**
 * @brief Applies deterministic stream baseline by resetting SFID index 0.
 */
app_status_t lan9645_stream_apply_baseline(lan9645_core_t *core)
{
  app_status_t st = lan9645_stream_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (s_stream_initialized == 0u) {
    (void)lan9645_stream_init();
  }

  return lan9645_stream_set_sfid_cfg(core, LAN9645_STREAM_BASE_INDEX, 0u, 0u, 0u, 0u);
}
