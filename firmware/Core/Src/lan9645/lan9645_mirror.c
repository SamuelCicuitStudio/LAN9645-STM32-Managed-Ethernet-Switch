#include "lan9645/lan9645_mirror.h"

/**
 * @file lan9645_mirror.c
 * @brief Baseline mirror domain configuration aligned with Linux register path.
 */

#include "lan9645/lan9645_reg.h"
#include "lan9645/lan9645_regs.h"

static const uint8_t s_mirror_ports[] = {0u, 1u, 8u};
static uint8_t s_mirror_initialized = 0u;
static const uint8_t s_mirror_port_max = 8u;

/**
 * @brief Validates that the core/register layer is ready for mirror operations.
 */
static app_status_t lan9645_mirror_validate_core(const lan9645_core_t *core)
{
  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }

  return APP_OK;
}

/**
 * @brief Validates one mirror source port index.
 */
static app_status_t lan9645_mirror_validate_port(uint8_t port)
{
  if (port > s_mirror_port_max) {
    return APP_EINVAL;
  }

  return APP_OK;
}

/**
 * @brief Writes one descriptor field and verifies masked readback.
 */
static app_status_t lan9645_mirror_rmw_verify(
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
 * @brief Initializes mirror service state.
 */
app_status_t lan9645_mirror_init(void)
{
  s_mirror_initialized = 1u;
  return APP_OK;
}

/**
 * @brief Reads global mirror destination masks.
 */
app_status_t lan9645_mirror_get_global_cfg(
  lan9645_core_t *core,
  uint16_t *ingress_mask,
  uint16_t *egress_mask
)
{
  uint32_t rd_ing = 0u;
  uint32_t rd_egr = 0u;
  app_status_t st = lan9645_mirror_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if ((ingress_mask == NULL) || (egress_mask == NULL)) {
    return APP_EINVAL;
  }

  st = lan9645_reg_read32_desc(
    core->reg,
    &LAN9645_REG_ANA_MIRRORPORTS,
    0u,
    0u,
    0u,
    &rd_ing
  );
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_reg_read32_desc(
    core->reg,
    &LAN9645_REG_ANA_EMIRRORPORTS,
    0u,
    0u,
    0u,
    &rd_egr
  );
  if (st != APP_OK) {
    return st;
  }

  *ingress_mask = (uint16_t)LAN9645_FIELD_GET(LAN9645_ANA_MIRRORPORTS_MIRRORPORTS, rd_ing);
  *egress_mask = (uint16_t)LAN9645_FIELD_GET(LAN9645_ANA_EMIRRORPORTS_EMIRRORPORTS, rd_egr);

  return APP_OK;
}

/**
 * @brief Writes global mirror destination masks with readback verification.
 */
app_status_t lan9645_mirror_set_global_cfg(
  lan9645_core_t *core,
  uint16_t ingress_mask,
  uint16_t egress_mask
)
{
  app_status_t st = lan9645_mirror_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if ((ingress_mask > LAN9645_MIRROR_PORT_MASK_MAX) ||
      (egress_mask > LAN9645_MIRROR_PORT_MASK_MAX)) {
    return APP_EINVAL;
  }

  st = lan9645_mirror_rmw_verify(
    core,
    &LAN9645_REG_ANA_MIRRORPORTS,
    0u,
    0u,
    LAN9645_ANA_MIRRORPORTS_MIRRORPORTS,
    LAN9645_ANA_MIRRORPORTS_MIRRORPORTS_SET((uint32_t)ingress_mask)
  );
  if (st != APP_OK) {
    return st;
  }

  return lan9645_mirror_rmw_verify(
    core,
    &LAN9645_REG_ANA_EMIRRORPORTS,
    0u,
    0u,
    LAN9645_ANA_EMIRRORPORTS_EMIRRORPORTS,
    LAN9645_ANA_EMIRRORPORTS_EMIRRORPORTS_SET((uint32_t)egress_mask)
  );
}

/**
 * @brief Reads per-port ingress mirror-source enable state.
 */
app_status_t lan9645_mirror_get_src_port(
  lan9645_core_t *core,
  uint8_t port,
  uint8_t *enable
)
{
  uint32_t rd = 0u;
  app_status_t st = lan9645_mirror_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_mirror_validate_port(port);
  if (st != APP_OK) {
    return st;
  }
  if (enable == NULL) {
    return APP_EINVAL;
  }

  st = lan9645_reg_read32_desc(
    core->reg,
    &LAN9645_REG_ANA_PORT_CFG,
    0u,
    (uint32_t)port,
    0u,
    &rd
  );
  if (st != APP_OK) {
    return st;
  }

  *enable = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_PORT_CFG_SRC_MIRROR_ENA, rd);
  return APP_OK;
}

/**
 * @brief Writes per-port ingress mirror-source enable state with readback verification.
 */
app_status_t lan9645_mirror_set_src_port(
  lan9645_core_t *core,
  uint8_t port,
  uint8_t enable
)
{
  app_status_t st = lan9645_mirror_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_mirror_validate_port(port);
  if (st != APP_OK) {
    return st;
  }
  if (enable > 1u) {
    return APP_EINVAL;
  }

  return lan9645_mirror_rmw_verify(
    core,
    &LAN9645_REG_ANA_PORT_CFG,
    (uint32_t)port,
    0u,
    LAN9645_ANA_PORT_CFG_SRC_MIRROR_ENA,
    LAN9645_ANA_PORT_CFG_SRC_MIRROR_ENA_SET((uint32_t)enable)
  );
}

/**
 * @brief Applies safe-off mirror baseline on managed ports and global registers.
 */
app_status_t lan9645_mirror_apply_baseline(lan9645_core_t *core)
{
  app_status_t st = lan9645_mirror_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (s_mirror_initialized == 0u) {
    (void)lan9645_mirror_init();
  }

  st = lan9645_mirror_set_global_cfg(core, 0u, 0u);
  if (st != APP_OK) {
    return st;
  }

  for (uint32_t i = 0u; i < (uint32_t)(sizeof(s_mirror_ports) / sizeof(s_mirror_ports[0])); ++i) {
    st = lan9645_mirror_set_src_port(core, s_mirror_ports[i], 0u);
    if (st != APP_OK) {
      return st;
    }
  }

  return APP_OK;
}
