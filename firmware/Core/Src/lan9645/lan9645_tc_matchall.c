#include "lan9645/lan9645_tc_matchall.h"

/**
 * @file lan9645_tc_matchall.c
 * @brief Baseline tc-matchall configuration aligned with LAN9645 VCAP baseline.
 */

#include "lan9645/lan9645_vcap.h"

static uint8_t s_tc_matchall_initialized = 0u;

/**
 * @brief Validates that tc-matchall operations can access a ready core/register layer.
 */
static app_status_t lan9645_tc_matchall_validate_core(const lan9645_core_t *core)
{
  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }

  return APP_OK;
}

/**
 * @brief Validates one managed port index used by tc-matchall runtime controls.
 */
static app_status_t lan9645_tc_matchall_validate_port(uint8_t port)
{
  if ((port != 0u) && (port != 1u) && (port != 8u)) {
    return APP_EINVAL;
  }

  return APP_OK;
}

/**
 * @brief Initializes tc-matchall baseline service state.
 */
app_status_t lan9645_tc_matchall_init(void)
{
  s_tc_matchall_initialized = 1u;
  return APP_OK;
}

/**
 * @brief Reads one managed-port tc-matchall runtime state.
 */
app_status_t lan9645_tc_matchall_get_state(
  lan9645_core_t *core,
  uint8_t port,
  lan9645_tc_matchall_state_t *out
)
{
  lan9645_vcap_port_cfg_t vcfg = {0};
  app_status_t st = lan9645_tc_matchall_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_tc_matchall_validate_port(port);
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
  out->enable = (vcfg.s2_ena != 0u) ? 1u : 0u;
  out->s2_ena = vcfg.s2_ena;
  out->s2_isdx_ena = vcfg.s2_isdx_ena;
  out->s2_udp_payload_ena = vcfg.s2_udp_payload_ena;
  out->s2_etype_payload_ena = vcfg.s2_etype_payload_ena;
  out->s2_ip6_cfg = vcfg.s2_ip6_cfg;
  return APP_OK;
}

/**
 * @brief Enables or disables tc-matchall classification state on one managed port.
 */
app_status_t lan9645_tc_matchall_set_enable(
  lan9645_core_t *core,
  uint8_t port,
  uint8_t enable
)
{
  lan9645_vcap_port_cfg_t vcfg = {0};
  app_status_t st = lan9645_tc_matchall_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_tc_matchall_validate_port(port);
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
    vcfg.s2_ena = 1u;
    vcfg.s2_isdx_ena = 1u;
    vcfg.s2_udp_payload_ena = 1u;
    vcfg.s2_etype_payload_ena = 1u;
  } else {
    vcfg.s2_ena = 0u;
    vcfg.s2_isdx_ena = 0u;
    vcfg.s2_udp_payload_ena = 0u;
    vcfg.s2_etype_payload_ena = 0u;
    vcfg.s2_ip6_cfg = 0u;
  }

  return lan9645_vcap_set_port_cfg(core, &vcfg);
}

/**
 * @brief Applies deterministic tc-matchall baseline by reusing VCAP no-rule baseline.
 */
app_status_t lan9645_tc_matchall_apply_baseline(lan9645_core_t *core)
{
  app_status_t st = lan9645_tc_matchall_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (s_tc_matchall_initialized == 0u) {
    (void)lan9645_tc_matchall_init();
  }

  return lan9645_vcap_apply_baseline(core);
}
