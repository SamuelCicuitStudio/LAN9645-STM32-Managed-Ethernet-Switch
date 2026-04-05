#include "lan9645/lan9645_ptp.h"

/**
 * @file lan9645_ptp.c
 * @brief PTP-domain configuration aligned with Linux LAN9645 behavior.
 */

#include "lan9645/lan9645_reg.h"
#include "lan9645/lan9645_regs.h"

static uint8_t s_ptp_initialized = 0u;

/**
 * @brief Validates that PTP operations can access a ready core/register layer.
 */
static app_status_t lan9645_ptp_validate_core(const lan9645_core_t *core)
{
  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }

  return APP_OK;
}

/**
 * @brief Validates one managed port used by PTP runtime control.
 */
static app_status_t lan9645_ptp_validate_port(uint8_t port)
{
  if ((port != 0u) && (port != 1u) && (port != 8u)) {
    return APP_EINVAL;
  }

  return APP_OK;
}

/**
 * @brief Reads `wait_for_ts` state from one front-port mode register.
 */
static app_status_t lan9645_ptp_read_wait_for_ts(
  lan9645_core_t *core,
  uint8_t port,
  uint8_t *out_wait_for_ts
)
{
  uint32_t rd = 0u;
  app_status_t st = lan9645_ptp_validate_port(port);
  if (st != APP_OK) {
    return st;
  }
  if (out_wait_for_ts == NULL) {
    return APP_EINVAL;
  }

  st = lan9645_reg_read32_desc(
    core->reg,
    &LAN9645_REG_SYS_FRONT_PORT_MODE,
    0u,
    (uint32_t)port,
    0u,
    &rd
  );
  if (st != APP_OK) {
    return st;
  }

  *out_wait_for_ts = ((rd & LAN9645_SYS_FRONT_PORT_MODE_DONT_WAIT_FOR_TS) == 0u) ? 1u : 0u;
  return APP_OK;
}

/**
 * @brief Writes one front-port `wait_for_ts` state and verifies readback.
 */
static app_status_t lan9645_ptp_write_wait_for_ts(
  lan9645_core_t *core,
  uint8_t port,
  uint8_t wait_for_ts
)
{
  const uint32_t value = LAN9645_SYS_FRONT_PORT_MODE_DONT_WAIT_FOR_TS_SET((wait_for_ts != 0u) ? 0u : 1u);
  uint8_t rd_wait_for_ts = 0u;
  app_status_t st = lan9645_ptp_validate_port(port);
  if (st != APP_OK) {
    return st;
  }
  if (wait_for_ts > LAN9645_PTP_BOOL_MAX) {
    return APP_EINVAL;
  }

  st = lan9645_reg_rmw32_desc(
    core->reg,
    &LAN9645_REG_SYS_FRONT_PORT_MODE,
    0u,
    (uint32_t)port,
    0u,
    LAN9645_SYS_FRONT_PORT_MODE_DONT_WAIT_FOR_TS,
    value
  );
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_ptp_read_wait_for_ts(core, port, &rd_wait_for_ts);
  if (st != APP_OK) {
    return st;
  }
  if (rd_wait_for_ts != wait_for_ts) {
    return APP_EIO;
  }

  return APP_OK;
}

/**
 * @brief Reads global PTP enable state from feature-disable register.
 */
static app_status_t lan9645_ptp_read_global_enable(lan9645_core_t *core, uint8_t *out_enable)
{
  uint32_t rd = 0u;
  app_status_t st = APP_OK;

  if (out_enable == NULL) {
    return APP_EINVAL;
  }

  st = lan9645_reg_read32_desc(
    core->reg,
    &LAN9645_REG_GCB_FEAT_DISABLE,
    0u,
    0u,
    0u,
    &rd
  );
  if (st != APP_OK) {
    return st;
  }

  *out_enable = ((rd & LAN9645_GCB_FEAT_DISABLE_FEAT_PTP_DIS) == 0u) ? 1u : 0u;
  return APP_OK;
}

/**
 * @brief Writes global PTP enable state and verifies readback.
 */
static app_status_t lan9645_ptp_write_global_enable(lan9645_core_t *core, uint8_t enable)
{
  const uint32_t value = (enable != 0u) ? 0u : LAN9645_GCB_FEAT_DISABLE_FEAT_PTP_DIS;
  uint8_t rd_enable = 0u;
  app_status_t st = APP_OK;

  if (enable > LAN9645_PTP_BOOL_MAX) {
    return APP_EINVAL;
  }

  st = lan9645_reg_rmw32_desc(
    core->reg,
    &LAN9645_REG_GCB_FEAT_DISABLE,
    0u,
    0u,
    0u,
    LAN9645_GCB_FEAT_DISABLE_FEAT_PTP_DIS,
    value
  );
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_ptp_read_global_enable(core, &rd_enable);
  if (st != APP_OK) {
    return st;
  }
  if (rd_enable != enable) {
    return APP_EIO;
  }

  return APP_OK;
}

/**
 * @brief Initializes PTP baseline service state.
 */
app_status_t lan9645_ptp_init(void)
{
  s_ptp_initialized = 1u;
  return APP_OK;
}

/**
 * @brief Reads PTP runtime configuration.
 */
app_status_t lan9645_ptp_get_cfg(lan9645_core_t *core, lan9645_ptp_cfg_t *out)
{
  app_status_t st = lan9645_ptp_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (out == NULL) {
    return APP_EINVAL;
  }

  st = lan9645_ptp_read_global_enable(core, &out->ptp_enable);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_ptp_read_wait_for_ts(core, 0u, &out->wait_for_ts_port0);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_ptp_read_wait_for_ts(core, 1u, &out->wait_for_ts_port1);
  if (st != APP_OK) {
    return st;
  }
  return lan9645_ptp_read_wait_for_ts(core, 8u, &out->wait_for_ts_port8);
}

/**
 * @brief Writes PTP runtime configuration with readback verification.
 */
app_status_t lan9645_ptp_set_cfg(lan9645_core_t *core, const lan9645_ptp_cfg_t *cfg)
{
  app_status_t st = lan9645_ptp_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (cfg == NULL) {
    return APP_EINVAL;
  }
  if ((cfg->ptp_enable > LAN9645_PTP_BOOL_MAX) ||
      (cfg->wait_for_ts_port0 > LAN9645_PTP_BOOL_MAX) ||
      (cfg->wait_for_ts_port1 > LAN9645_PTP_BOOL_MAX) ||
      (cfg->wait_for_ts_port8 > LAN9645_PTP_BOOL_MAX)) {
    return APP_EINVAL;
  }

  st = lan9645_ptp_write_global_enable(core, cfg->ptp_enable);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_ptp_write_wait_for_ts(core, 0u, cfg->wait_for_ts_port0);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_ptp_write_wait_for_ts(core, 1u, cfg->wait_for_ts_port1);
  if (st != APP_OK) {
    return st;
  }
  return lan9645_ptp_write_wait_for_ts(core, 8u, cfg->wait_for_ts_port8);
}

/**
 * @brief Applies deterministic PTP baseline.
 */
app_status_t lan9645_ptp_apply_baseline(lan9645_core_t *core)
{
  lan9645_ptp_cfg_t cfg = {0};
  app_status_t st = lan9645_ptp_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (s_ptp_initialized == 0u) {
    (void)lan9645_ptp_init();
  }

  cfg.ptp_enable = 1u;
  cfg.wait_for_ts_port0 = 1u;
  cfg.wait_for_ts_port1 = 1u;
  cfg.wait_for_ts_port8 = 1u;
  return lan9645_ptp_set_cfg(core, &cfg);
}
