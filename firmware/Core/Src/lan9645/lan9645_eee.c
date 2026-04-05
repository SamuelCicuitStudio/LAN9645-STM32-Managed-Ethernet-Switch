#include "lan9645/lan9645_eee.h"

/**
 * @file lan9645_eee.c
 * @brief Baseline EEE control aligned with Linux LAN9645 DEV_EEE_CFG behavior.
 */

#include "lan9645/lan9645_reg.h"
#include "lan9645/lan9645_regs.h"

static const uint8_t s_eee_ports[] = {0u, 1u, 8u};
static uint8_t s_eee_initialized = 0u;
static const uint8_t s_eee_port_max = 8u;

/**
 * @brief Validates that EEE operations can access an initialized register layer.
 */
static app_status_t lan9645_eee_validate_core(const lan9645_core_t *core)
{
  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }

  return APP_OK;
}

/**
 * @brief Validates one DEV port index for EEE operations.
 */
static app_status_t lan9645_eee_validate_port(uint8_t port)
{
  if (port > s_eee_port_max) {
    return APP_EINVAL;
  }

  return APP_OK;
}

/**
 * @brief Performs one descriptor RMW and verifies masked readback.
 */
static app_status_t lan9645_eee_rmw_verify(
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
 * @brief Reads one EEE configuration snapshot from DEV_EEE_CFG.
 */
app_status_t lan9645_eee_get_port_cfg(
  lan9645_core_t *core,
  uint8_t port,
  lan9645_eee_port_cfg_t *out
)
{
  uint32_t rd = 0u;
  app_status_t st = lan9645_eee_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (out == NULL) {
    return APP_EINVAL;
  }
  st = lan9645_eee_validate_port(port);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_reg_read32_desc(
    core->reg,
    &LAN9645_REG_DEV_EEE_CFG,
    0u,
    (uint32_t)port,
    0u,
    &rd
  );
  if (st != APP_OK) {
    return st;
  }

  out->port = port;
  out->eee_enable = (uint8_t)LAN9645_FIELD_GET(LAN9645_DEV_EEE_CFG_EEE_ENA, rd);
  out->wakeup_timer = (uint8_t)LAN9645_FIELD_GET(LAN9645_DEV_EEE_CFG_EEE_TIMER_WAKEUP, rd);
  out->holdoff_timer = (uint8_t)LAN9645_FIELD_GET(LAN9645_DEV_EEE_CFG_EEE_TIMER_HOLDOFF, rd);
  out->age_timer = (uint8_t)LAN9645_FIELD_GET(LAN9645_DEV_EEE_CFG_EEE_TIMER_AGE, rd);
  out->port_lpi = (uint8_t)LAN9645_FIELD_GET(LAN9645_DEV_EEE_CFG_PORT_LPI, rd);

  return APP_OK;
}

/**
 * @brief Writes EEE enable/wakeup/holdoff fields for one DEV port.
 */
app_status_t lan9645_eee_set_port_cfg(
  lan9645_core_t *core,
  uint8_t port,
  uint8_t eee_enable,
  uint8_t wakeup_timer,
  uint8_t holdoff_timer
)
{
  const uint32_t mask =
    LAN9645_DEV_EEE_CFG_EEE_ENA |
    LAN9645_DEV_EEE_CFG_EEE_TIMER_WAKEUP |
    LAN9645_DEV_EEE_CFG_EEE_TIMER_HOLDOFF;
  const uint32_t value =
    LAN9645_DEV_EEE_CFG_EEE_ENA_SET((uint32_t)eee_enable) |
    LAN9645_DEV_EEE_CFG_EEE_TIMER_WAKEUP_SET((uint32_t)wakeup_timer) |
    LAN9645_DEV_EEE_CFG_EEE_TIMER_HOLDOFF_SET((uint32_t)holdoff_timer);
  app_status_t st = lan9645_eee_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_eee_validate_port(port);
  if (st != APP_OK) {
    return st;
  }
  if ((eee_enable > 1u) ||
      (wakeup_timer > LAN9645_EEE_WAKEUP_TIMER_MAX) ||
      (holdoff_timer > LAN9645_EEE_HOLDOFF_TIMER_MAX)) {
    return APP_EINVAL;
  }

  return lan9645_eee_rmw_verify(
    core,
    &LAN9645_REG_DEV_EEE_CFG,
    (uint32_t)port,
    0u,
    mask,
    value
  );
}

/**
 * @brief Applies EEE baseline configuration on one managed port.
 */
static app_status_t lan9645_eee_apply_port_baseline(lan9645_core_t *core, uint8_t port)
{
  return lan9645_eee_set_port_cfg(core, port, 0u, 0u, 0u);
}

/**
 * @brief Initializes EEE baseline service state.
 */
app_status_t lan9645_eee_init(void)
{
  s_eee_initialized = 1u;
  return APP_OK;
}

/**
 * @brief Applies deterministic EEE baseline for managed ports.
 */
app_status_t lan9645_eee_apply_baseline(lan9645_core_t *core)
{
  app_status_t st = lan9645_eee_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (s_eee_initialized == 0u) {
    (void)lan9645_eee_init();
  }

  for (uint32_t i = 0u; i < (uint32_t)(sizeof(s_eee_ports) / sizeof(s_eee_ports[0])); ++i) {
    st = lan9645_eee_apply_port_baseline(core, s_eee_ports[i]);
    if (st != APP_OK) {
      return st;
    }
  }

  return APP_OK;
}
