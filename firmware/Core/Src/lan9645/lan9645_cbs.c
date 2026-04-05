#include "lan9645/lan9645_cbs.h"

/**
 * @file lan9645_cbs.c
 * @brief Baseline CBS disable path aligned with Linux LAN9645 queue scheduler behavior.
 */

#include "lan9645/lan9645_reg.h"
#include "lan9645/lan9645_regs.h"

static const uint8_t s_cbs_ports[] = {0u, 1u, 8u};
static uint8_t s_cbs_initialized = 0u;

/**
 * @brief Validates that CBS operations can access an initialized register layer.
 */
static app_status_t lan9645_cbs_validate_core(const lan9645_core_t *core)
{
  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  return APP_OK;
}

/**
 * @brief Validates one CBS queue configuration tuple before register programming.
 */
static app_status_t lan9645_cbs_validate_cfg_values(
  uint8_t queue,
  uint8_t avb_enable,
  uint8_t frm_mode,
  uint16_t cir_rate,
  uint8_t cir_burst
)
{
  if ((queue >= LAN9645_CBS_QUEUE_COUNT) ||
      (avb_enable > 1u) ||
      (frm_mode > LAN9645_CBS_FRM_MODE_MAX) ||
      (cir_rate > LAN9645_CBS_CIR_RATE_MAX) ||
      (cir_burst > LAN9645_CBS_CIR_BURST_MAX)) {
    return APP_EINVAL;
  }
  return APP_OK;
}

/**
 * @brief Performs a descriptor RMW and verifies masked readback.
 */
static app_status_t lan9645_cbs_rmw_verify(
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
static app_status_t lan9645_cbs_write_verify(
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
 * @brief Converts managed port + queue to Linux-aligned queue scheduler element index.
 */
static uint32_t lan9645_cbs_queue_se_index(uint8_t port, uint8_t queue)
{
  return ((uint32_t)port * LAN9645_CBS_QUEUE_COUNT) + (uint32_t)queue;
}

/**
 * @brief Reads one queue scheduler-element CBS configuration.
 */
app_status_t lan9645_cbs_get_queue_cfg(
  lan9645_core_t *core,
  uint8_t port,
  uint8_t queue,
  lan9645_cbs_cfg_t *out
)
{
  uint32_t se_cfg = 0u;
  uint32_t cir_cfg = 0u;
  const uint32_t se_idx = lan9645_cbs_queue_se_index(port, queue);
  app_status_t st = lan9645_cbs_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (out == NULL) {
    return APP_EINVAL;
  }
  st = lan9645_cbs_validate_cfg_values(queue, 0u, 0u, 0u, 0u);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_reg_read32_desc(core->reg, &LAN9645_REG_QSYS_SE_CFG, se_idx, 0u, 0u, &se_cfg);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_reg_read32_desc(core->reg, &LAN9645_REG_QSYS_CIR_CFG, se_idx, 0u, 0u, &cir_cfg);
  if (st != APP_OK) {
    return st;
  }

  out->port = port;
  out->queue = queue;
  out->avb_enable = (uint8_t)LAN9645_FIELD_GET(LAN9645_QSYS_SE_CFG_SE_AVB_ENA, se_cfg);
  out->frm_mode = (uint8_t)LAN9645_FIELD_GET(LAN9645_QSYS_SE_CFG_SE_FRM_MODE, se_cfg);
  out->cir_rate = (uint16_t)LAN9645_FIELD_GET(LAN9645_QSYS_CIR_CFG_CIR_RATE, cir_cfg);
  out->cir_burst = (uint8_t)LAN9645_FIELD_GET(LAN9645_QSYS_CIR_CFG_CIR_BURST, cir_cfg);
  return APP_OK;
}

/**
 * @brief Writes one queue scheduler-element CBS configuration and verifies readback.
 */
app_status_t lan9645_cbs_set_queue_cfg(
  lan9645_core_t *core,
  uint8_t port,
  uint8_t queue,
  uint8_t avb_enable,
  uint8_t frm_mode,
  uint16_t cir_rate,
  uint8_t cir_burst
)
{
  const uint32_t se_mask =
    LAN9645_QSYS_SE_CFG_SE_AVB_ENA |
    LAN9645_QSYS_SE_CFG_SE_FRM_MODE;
  const uint32_t se_idx = lan9645_cbs_queue_se_index(port, queue);
  app_status_t st = lan9645_cbs_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_cbs_validate_cfg_values(queue, avb_enable, frm_mode, cir_rate, cir_burst);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_cbs_rmw_verify(
    core,
    &LAN9645_REG_QSYS_SE_CFG,
    se_idx,
    0u,
    se_mask,
    LAN9645_QSYS_SE_CFG_SE_AVB_ENA_SET(avb_enable) |
      LAN9645_QSYS_SE_CFG_SE_FRM_MODE_SET(frm_mode)
  );
  if (st != APP_OK) {
    return st;
  }

  return lan9645_cbs_write_verify(
    core,
    &LAN9645_REG_QSYS_CIR_CFG,
    se_idx,
    0u,
    LAN9645_QSYS_CIR_CFG_CIR_RATE_SET(cir_rate) |
      LAN9645_QSYS_CIR_CFG_CIR_BURST_SET(cir_burst),
    LAN9645_QSYS_CIR_CFG_CIR_RATE |
      LAN9645_QSYS_CIR_CFG_CIR_BURST
  );
}

/**
 * @brief Initializes CBS baseline service state.
 */
app_status_t lan9645_cbs_init(void)
{
  s_cbs_initialized = 1u;
  return APP_OK;
}

/**
 * @brief Applies deterministic CBS baseline by disabling shaping on managed queue schedulers.
 */
app_status_t lan9645_cbs_apply_baseline(lan9645_core_t *core)
{
  app_status_t st = lan9645_cbs_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (s_cbs_initialized == 0u) {
    (void)lan9645_cbs_init();
  }

  for (uint32_t i = 0u; i < (uint32_t)(sizeof(s_cbs_ports) / sizeof(s_cbs_ports[0])); ++i) {
    const uint8_t port = s_cbs_ports[i];
    for (uint32_t q = 0u; q < LAN9645_CBS_QUEUE_COUNT; ++q) {
      st = lan9645_cbs_set_queue_cfg(core, port, (uint8_t)q, 1u, 0u, 0u, 0u);
      if (st != APP_OK) {
        return st;
      }
    }
  }

  return APP_OK;
}
