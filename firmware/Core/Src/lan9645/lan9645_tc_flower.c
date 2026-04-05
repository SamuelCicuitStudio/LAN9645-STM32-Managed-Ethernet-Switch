#include "lan9645/lan9645_tc_flower.h"

/**
 * @file lan9645_tc_flower.c
 * @brief Baseline tc-flower configuration aligned with LAN9645 VCAP baseline.
 */

#include "lan9645/lan9645_vcap.h"

static uint8_t s_tc_flower_initialized = 0u;

/**
 * @brief Validates that tc-flower operations can access a ready core/register layer.
 */
static app_status_t lan9645_tc_flower_validate_core(const lan9645_core_t *core)
{
  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }

  return APP_OK;
}

/**
 * @brief Validates one managed port index used by tc-flower runtime controls.
 */
static app_status_t lan9645_tc_flower_validate_port(uint8_t port)
{
  if ((port != 0u) && (port != 1u) && (port != 8u)) {
    return APP_EINVAL;
  }

  return APP_OK;
}

/**
 * @brief Initializes tc-flower baseline service state.
 */
app_status_t lan9645_tc_flower_init(void)
{
  s_tc_flower_initialized = 1u;
  return APP_OK;
}

/**
 * @brief Reads one managed-port tc-flower runtime state.
 */
app_status_t lan9645_tc_flower_get_state(
  lan9645_core_t *core,
  uint8_t port,
  lan9645_tc_flower_state_t *out
)
{
  lan9645_vcap_port_cfg_t vcfg = {0};
  app_status_t st = lan9645_tc_flower_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_tc_flower_validate_port(port);
  if (st != APP_OK) {
    return st;
  }
  if (out == NULL) {
    return APP_EINVAL;
  }

  st = lan9645_vcap_get_port_cfg(core, port, &vcfg);
  if (st != APP_OK) {
    return st;
  }

  out->port = port;
  out->enable = (vcfg.s1_ena != 0u) ? 1u : 0u;
  out->s1_smac_ena = vcfg.s1_smac_ena;
  out->s1_ena = vcfg.s1_ena;
  out->s1_dmac_dip_ena = vcfg.s1_dmac_dip_ena;
  out->s1_vlan_inner_tag_ena = vcfg.s1_vlan_inner_tag_ena;
  out->s1_pag_val = vcfg.s1_pag_val;
  return APP_OK;
}

/**
 * @brief Enables or disables tc-flower classification state on one managed port.
 */
app_status_t lan9645_tc_flower_set_enable(
  lan9645_core_t *core,
  uint8_t port,
  uint8_t enable
)
{
  lan9645_vcap_port_cfg_t vcfg = {0};
  app_status_t st = lan9645_tc_flower_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_tc_flower_validate_port(port);
  if (st != APP_OK) {
    return st;
  }
  if (enable > 1u) {
    return APP_EINVAL;
  }

  st = lan9645_vcap_get_port_cfg(core, port, &vcfg);
  if (st != APP_OK) {
    return st;
  }

  if (enable != 0u) {
    vcfg.s1_smac_ena = 1u;
    vcfg.s1_ena = 1u;
    vcfg.s1_dmac_dip_ena = 1u;
    vcfg.s1_vlan_inner_tag_ena = 1u;
  } else {
    vcfg.s1_smac_ena = 0u;
    vcfg.s1_ena = 0u;
    vcfg.s1_dmac_dip_ena = 0u;
    vcfg.s1_vlan_inner_tag_ena = 0u;
    vcfg.s1_pag_val = 0u;
  }

  return lan9645_vcap_set_port_cfg(core, &vcfg);
}

/**
 * @brief Applies deterministic tc-flower baseline by reusing VCAP no-rule baseline.
 */
app_status_t lan9645_tc_flower_apply_baseline(lan9645_core_t *core)
{
  app_status_t st = lan9645_tc_flower_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (s_tc_flower_initialized == 0u) {
    (void)lan9645_tc_flower_init();
  }

  return lan9645_vcap_apply_baseline(core);
}
