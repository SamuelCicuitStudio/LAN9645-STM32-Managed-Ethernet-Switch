#include "lan9645/lan9645_qos.h"

/**
 * @file lan9645_qos.c
 * @brief Baseline QoS/mqprio/ETS helpers aligned with Linux LAN9645 behavior.
 */

#include <string.h>

#include "diag/diag_core.h"
#include "diag/diag_router.h"
#include "lan9645/lan9645_reg.h"
#include "lan9645/lan9645_regs.h"

#define LAN9645_QOS_PRIO_COUNT          (8u)
#define LAN9645_QOS_DP_COUNT            (2u)
#define LAN9645_QOS_DSCP_COUNT          (64u)
#define LAN9645_QOS_SE_DWRR_COUNT       (8u)
#define LAN9645_QOS_PORT_SCHED_BASE     (88u)
#define LAN9645_QOS_DWRR_DEFAULT_CNT    (7u)
#define LAN9645_QOS_DWRR_DEFAULT_COST   (1u)

static const uint8_t s_qos_ports[] = {0u, 1u, 8u};
static uint8_t s_qos_initialized = 0u;

/**
 * @brief Reports one QoS operation failure into diagnostics and routed logs.
 */
static void lan9645_qos_report_fail(uint8_t port, uint32_t reg_tag, app_status_t st, uint32_t step)
{
  diag_event_t evt = {0};

  evt.id = DIAG_EVT_LAN_FEAT_CFG_FAIL;
  evt.severity = DIAG_SEV_ERR;
  evt.dev = (uint32_t)port;
  evt.reg = reg_tag;
  evt.arg0 = (uint32_t)(int32_t)st;
  evt.arg1 = step;

  (void)diag_core_report(&evt);
  (void)diag_router_write_event(&evt);
}

/**
 * @brief Validates that QoS operations can access a ready core/register context.
 */
static app_status_t lan9645_qos_validate_core(const lan9645_core_t *core)
{
  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }

  return APP_OK;
}

/**
 * @brief Returns 1 when the port belongs to the managed baseline scope (0, 1, 8).
 */
static uint8_t lan9645_qos_is_managed_port(uint8_t port)
{
  if ((port == 0u) || (port == 1u) || (port == 8u)) {
    return 1u;
  }
  return 0u;
}

/**
 * @brief Returns scheduler-element index for one managed port.
 */
static uint32_t lan9645_qos_sched_se_idx(uint8_t port)
{
  return LAN9645_QOS_PORT_SCHED_BASE + (uint32_t)port;
}

/**
 * @brief Reads one managed-port ANA_QOS_CFG register value.
 */
static app_status_t lan9645_qos_read_port_qos_cfg(lan9645_core_t *core, uint8_t port, uint32_t *out)
{
  if (out == NULL) {
    return APP_EINVAL;
  }
  return lan9645_reg_read32_desc(core->reg, &LAN9645_REG_ANA_QOS_CFG, 0u, (uint32_t)port, 0u, out);
}

/**
 * @brief Writes one descriptor register and verifies masked readback.
 */
static app_status_t lan9645_qos_write_verify_mask(
  lan9645_core_t *core,
  const lan9645_reg_desc_t *desc,
  uint32_t group_instance,
  uint32_t reg_instance,
  uint32_t value,
  uint32_t verify_mask
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

  if ((rd & verify_mask) != (value & verify_mask)) {
    return APP_EIO;
  }

  return APP_OK;
}

/**
 * @brief Performs one descriptor RMW and verifies masked readback.
 */
static app_status_t lan9645_qos_rmw_verify_mask(
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
 * @brief Applies ingress and egress QoS baseline for one switch port.
 */
static app_status_t lan9645_qos_apply_port_baseline(lan9645_core_t *core, uint8_t port)
{
  const uint32_t ana_qos_mask =
    LAN9645_ANA_QOS_CFG_DP_DEFAULT_VAL |
    LAN9645_ANA_QOS_CFG_QOS_DEFAULT_VAL |
    LAN9645_ANA_QOS_CFG_QOS_DSCP_ENA |
    LAN9645_ANA_QOS_CFG_QOS_PCP_ENA;
  const uint32_t ana_qos_value =
    LAN9645_ANA_QOS_CFG_DP_DEFAULT_VAL_SET(0u) |
    LAN9645_ANA_QOS_CFG_QOS_DEFAULT_VAL_SET(0u) |
    LAN9645_ANA_QOS_CFG_QOS_DSCP_ENA_SET(0u) |
    LAN9645_ANA_QOS_CFG_QOS_PCP_ENA_SET(1u);
  const uint32_t ana_pcp_dei_mask =
    LAN9645_ANA_PCP_DEI_CFG_DP_PCP_DEI_VAL |
    LAN9645_ANA_PCP_DEI_CFG_QOS_PCP_DEI_VAL;
  const uint32_t rew_port_vlan_mask =
    LAN9645_REW_PORT_VLAN_CFG_PORT_DEI |
    LAN9645_REW_PORT_VLAN_CFG_PORT_PCP;
  const uint32_t rew_port_vlan_value =
    LAN9645_REW_PORT_VLAN_CFG_PORT_DEI_SET(0u) |
    LAN9645_REW_PORT_VLAN_CFG_PORT_PCP_SET(0u);
  const uint32_t rew_pcp_dei_mask =
    LAN9645_REW_PCP_DEI_CFG_DEI_QOS_VAL |
    LAN9645_REW_PCP_DEI_CFG_PCP_QOS_VAL;
  const uint32_t rew_tag_mask =
    LAN9645_REW_TAG_CFG_TAG_PCP_CFG |
    LAN9645_REW_TAG_CFG_TAG_DEI_CFG;
  const uint32_t rew_tag_value =
    LAN9645_REW_TAG_CFG_TAG_PCP_CFG_SET(0u) |
    LAN9645_REW_TAG_CFG_TAG_DEI_CFG_SET(0u);
  app_status_t st = APP_OK;

  st = lan9645_qos_rmw_verify_mask(core, &LAN9645_REG_ANA_QOS_CFG, (uint32_t)port, 0u, ana_qos_mask, ana_qos_value);
  if (st != APP_OK) {
    return st;
  }

  for (uint32_t pcp = 0u; pcp < LAN9645_QOS_PRIO_COUNT; ++pcp) {
    for (uint32_t dei = 0u; dei < LAN9645_QOS_DP_COUNT; ++dei) {
      const uint32_t idx = (LAN9645_QOS_PRIO_COUNT * dei) + pcp;
      const uint32_t value =
        LAN9645_ANA_PCP_DEI_CFG_QOS_PCP_DEI_VAL_SET(pcp) |
        LAN9645_ANA_PCP_DEI_CFG_DP_PCP_DEI_VAL_SET(dei);
      st = lan9645_qos_write_verify_mask(core, &LAN9645_REG_ANA_PCP_DEI_CFG, (uint32_t)port, idx, value, ana_pcp_dei_mask);
      if (st != APP_OK) {
        return st;
      }
    }
  }

  st = lan9645_qos_rmw_verify_mask(
    core,
    &LAN9645_REG_REW_PORT_VLAN_CFG,
    (uint32_t)port,
    0u,
    rew_port_vlan_mask,
    rew_port_vlan_value
  );
  if (st != APP_OK) {
    return st;
  }

  for (uint32_t prio = 0u; prio < LAN9645_QOS_PRIO_COUNT; ++prio) {
    for (uint32_t dpl = 0u; dpl < LAN9645_QOS_DP_COUNT; ++dpl) {
      const uint32_t idx = (LAN9645_QOS_PRIO_COUNT * dpl) + prio;
      const uint32_t value =
        LAN9645_REW_PCP_DEI_CFG_PCP_QOS_VAL_SET(prio) |
        LAN9645_REW_PCP_DEI_CFG_DEI_QOS_VAL_SET(dpl);
      st = lan9645_qos_write_verify_mask(core, &LAN9645_REG_REW_PCP_DEI_CFG, (uint32_t)port, idx, value, rew_pcp_dei_mask);
      if (st != APP_OK) {
        return st;
      }
    }
  }

  st = lan9645_qos_rmw_verify_mask(core, &LAN9645_REG_REW_TAG_CFG, (uint32_t)port, 0u, rew_tag_mask, rew_tag_value);
  if (st != APP_OK) {
    return st;
  }

  return APP_OK;
}

/**
 * @brief Applies deterministic DSCP baseline map at switch-global scope.
 */
static app_status_t lan9645_qos_apply_dscp_baseline(lan9645_core_t *core)
{
  const uint32_t dscp_mask =
    LAN9645_ANA_DSCP_CFG_DP_DSCP_VAL |
    LAN9645_ANA_DSCP_CFG_QOS_DSCP_VAL |
    LAN9645_ANA_DSCP_CFG_DSCP_TRUST_ENA |
    LAN9645_ANA_DSCP_CFG_DSCP_REWR_ENA;
  const uint32_t dscp_value =
    LAN9645_ANA_DSCP_CFG_DP_DSCP_VAL_SET(0u) |
    LAN9645_ANA_DSCP_CFG_QOS_DSCP_VAL_SET(0u) |
    LAN9645_ANA_DSCP_CFG_DSCP_TRUST_ENA_SET(0u);
  app_status_t st = APP_OK;

  for (uint32_t dscp = 0u; dscp < LAN9645_QOS_DSCP_COUNT; ++dscp) {
    st = lan9645_qos_rmw_verify_mask(core, &LAN9645_REG_ANA_DSCP_CFG, 0u, dscp, dscp_mask, dscp_value);
    if (st != APP_OK) {
      return st;
    }
  }

  return APP_OK;
}

/**
 * @brief Applies scheduler mode for one managed port.
 */
static app_status_t lan9645_qos_apply_scheduler_port(
  lan9645_core_t *core,
  uint8_t port,
  lan9645_qos_sched_mode_t mode
)
{
  const uint32_t se_idx = lan9645_qos_sched_se_idx(port);
  const uint32_t se_cfg_mask =
    LAN9645_QSYS_SE_CFG_SE_DWRR_CNT |
    LAN9645_QSYS_SE_CFG_SE_RR_ENA;
  uint32_t se_cfg_value =
    LAN9645_QSYS_SE_CFG_SE_DWRR_CNT_SET(0u) |
    LAN9645_QSYS_SE_CFG_SE_RR_ENA_SET(0u);
  uint32_t cost_value = LAN9645_QSYS_SE_DWRR_CFG_DWRR_COST_SET(0u);
  app_status_t st = APP_OK;

  if (mode == LAN9645_QOS_SCHED_DWRR) {
    se_cfg_value =
      LAN9645_QSYS_SE_CFG_SE_DWRR_CNT_SET(LAN9645_QOS_DWRR_DEFAULT_CNT) |
      LAN9645_QSYS_SE_CFG_SE_RR_ENA_SET(1u);
    cost_value = LAN9645_QSYS_SE_DWRR_CFG_DWRR_COST_SET(LAN9645_QOS_DWRR_DEFAULT_COST);
  }

  for (uint32_t queue = 0u; queue < LAN9645_QOS_SE_DWRR_COUNT; ++queue) {
    st = lan9645_qos_write_verify_mask(
      core,
      &LAN9645_REG_QSYS_SE_DWRR_CFG,
      se_idx,
      queue,
      cost_value,
      LAN9645_QSYS_SE_DWRR_CFG_DWRR_COST
    );
    if (st != APP_OK) {
      return st;
    }
  }

  return lan9645_qos_rmw_verify_mask(core, &LAN9645_REG_QSYS_SE_CFG, se_idx, 0u, se_cfg_mask, se_cfg_value);
}

/**
 * @brief Initializes QoS baseline service state.
 */
app_status_t lan9645_qos_init(void)
{
  s_qos_initialized = 1u;
  return APP_OK;
}

/**
 * @brief Applies ingress/egress QoS baseline for managed ports 0, 1, and 8.
 */
app_status_t lan9645_qos_apply_baseline(lan9645_core_t *core)
{
  app_status_t st = lan9645_qos_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (s_qos_initialized == 0u) {
    (void)lan9645_qos_init();
  }

  for (uint32_t i = 0u; i < (uint32_t)(sizeof(s_qos_ports) / sizeof(s_qos_ports[0])); ++i) {
    st = lan9645_qos_apply_port_baseline(core, s_qos_ports[i]);
    if (st != APP_OK) {
      return st;
    }
  }

  return lan9645_qos_apply_dscp_baseline(core);
}

/**
 * @brief Applies strict scheduler baseline for managed ports 0, 1, and 8.
 */
app_status_t lan9645_qos_apply_mqprio_baseline(lan9645_core_t *core)
{
  app_status_t st = lan9645_qos_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (s_qos_initialized == 0u) {
    (void)lan9645_qos_init();
  }

  for (uint32_t i = 0u; i < (uint32_t)(sizeof(s_qos_ports) / sizeof(s_qos_ports[0])); ++i) {
    st = lan9645_qos_apply_scheduler_port(core, s_qos_ports[i], LAN9645_QOS_SCHED_STRICT);
    if (st != APP_OK) {
      return st;
    }
  }

  return APP_OK;
}

/**
 * @brief Applies default ETS baseline, equivalent to strict scheduler mode.
 */
app_status_t lan9645_qos_apply_ets_baseline(lan9645_core_t *core)
{
  return lan9645_qos_apply_mqprio_baseline(core);
}

/**
 * @brief Sets scheduler mode for one managed port and verifies readback path.
 */
app_status_t lan9645_qos_set_scheduler_mode(
  lan9645_core_t *core,
  uint8_t port,
  lan9645_qos_sched_mode_t mode
)
{
  app_status_t st = lan9645_qos_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (lan9645_qos_is_managed_port(port) == 0u) {
    return APP_EINVAL;
  }
  if ((mode != LAN9645_QOS_SCHED_STRICT) && (mode != LAN9645_QOS_SCHED_DWRR)) {
    return APP_EINVAL;
  }
  if (s_qos_initialized == 0u) {
    (void)lan9645_qos_init();
  }

  st = lan9645_qos_apply_scheduler_port(core, port, mode);
  if (st != APP_OK) {
    lan9645_qos_report_fail(
      port,
      lan9645_reg_desc_rel_addr(&LAN9645_REG_QSYS_SE_CFG, lan9645_qos_sched_se_idx(port), 0u),
      st,
      1u
    );
    return st;
  }

  return APP_OK;
}

/**
 * @brief Reads scheduler state for one managed port.
 */
app_status_t lan9645_qos_get_scheduler_state(
  lan9645_core_t *core,
  uint8_t port,
  lan9645_qos_sched_state_t *out
)
{
  uint32_t rd = 0u;
  uint32_t se_idx = 0u;
  app_status_t st = lan9645_qos_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if ((out == NULL) || (lan9645_qos_is_managed_port(port) == 0u)) {
    return APP_EINVAL;
  }

  memset(out, 0, sizeof(*out));
  out->port = port;
  se_idx = lan9645_qos_sched_se_idx(port);

  st = lan9645_reg_read32_desc(core->reg, &LAN9645_REG_QSYS_SE_CFG, 0u, se_idx, 0u, &rd);
  if (st != APP_OK) {
    return st;
  }
  out->rr_enable = (uint8_t)LAN9645_FIELD_GET(LAN9645_QSYS_SE_CFG_SE_RR_ENA, rd);
  out->dwrr_count = (uint8_t)LAN9645_FIELD_GET(LAN9645_QSYS_SE_CFG_SE_DWRR_CNT, rd);
  out->mode = (out->rr_enable != 0u) ? LAN9645_QOS_SCHED_DWRR : LAN9645_QOS_SCHED_STRICT;

  for (uint32_t queue = 0u; queue < LAN9645_QOS_SE_DWRR_COUNT; ++queue) {
    st = lan9645_reg_read32_desc(core->reg, &LAN9645_REG_QSYS_SE_DWRR_CFG, 0u, se_idx, queue, &rd);
    if (st != APP_OK) {
      return st;
    }
    out->cost[queue] = (uint8_t)LAN9645_FIELD_GET(LAN9645_QSYS_SE_DWRR_CFG_DWRR_COST, rd);
  }

  return APP_OK;
}

/**
 * @brief Sets one DWRR queue cost with readback verification.
 */
app_status_t lan9645_qos_set_scheduler_weight(
  lan9645_core_t *core,
  uint8_t port,
  uint8_t queue,
  uint8_t cost
)
{
  app_status_t st = lan9645_qos_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if ((lan9645_qos_is_managed_port(port) == 0u) || (queue >= LAN9645_QOS_SE_DWRR_COUNT) || (cost > LAN9645_QOS_DWRR_COST_MAX)) {
    return APP_EINVAL;
  }
  if (s_qos_initialized == 0u) {
    (void)lan9645_qos_init();
  }

  st = lan9645_qos_write_verify_mask(
    core,
    &LAN9645_REG_QSYS_SE_DWRR_CFG,
    lan9645_qos_sched_se_idx(port),
    (uint32_t)queue,
    LAN9645_QSYS_SE_DWRR_CFG_DWRR_COST_SET(cost),
    LAN9645_QSYS_SE_DWRR_CFG_DWRR_COST
  );
  if (st != APP_OK) {
    lan9645_qos_report_fail(
      port,
      lan9645_reg_desc_rel_addr(&LAN9645_REG_QSYS_SE_DWRR_CFG, lan9645_qos_sched_se_idx(port), (uint32_t)queue),
      st,
      2u
    );
    return st;
  }

  return APP_OK;
}

/**
 * @brief Reads one DWRR queue cost from scheduler configuration.
 */
app_status_t lan9645_qos_get_scheduler_weight(
  lan9645_core_t *core,
  uint8_t port,
  uint8_t queue,
  uint8_t *out_cost
)
{
  uint32_t rd = 0u;
  app_status_t st = lan9645_qos_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if ((out_cost == NULL) || (lan9645_qos_is_managed_port(port) == 0u) || (queue >= LAN9645_QOS_SE_DWRR_COUNT)) {
    return APP_EINVAL;
  }

  st = lan9645_reg_read32_desc(
    core->reg,
    &LAN9645_REG_QSYS_SE_DWRR_CFG,
    0u,
    lan9645_qos_sched_se_idx(port),
    (uint32_t)queue,
    &rd
  );
  if (st != APP_OK) {
    return st;
  }

  *out_cost = (uint8_t)LAN9645_FIELD_GET(LAN9645_QSYS_SE_DWRR_CFG_DWRR_COST, rd);
  return APP_OK;
}

/**
 * @brief Sets per-port QoS trust policy with readback verification.
 */
app_status_t lan9645_qos_set_port_trust(
  lan9645_core_t *core,
  uint8_t port,
  uint8_t trust_pcp,
  uint8_t trust_dscp
)
{
  const uint32_t mask =
    LAN9645_ANA_QOS_CFG_QOS_DSCP_ENA |
    LAN9645_ANA_QOS_CFG_QOS_PCP_ENA;
  const uint32_t value =
    LAN9645_ANA_QOS_CFG_QOS_DSCP_ENA_SET((trust_dscp != 0u) ? 1u : 0u) |
    LAN9645_ANA_QOS_CFG_QOS_PCP_ENA_SET((trust_pcp != 0u) ? 1u : 0u);
  app_status_t st = lan9645_qos_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (lan9645_qos_is_managed_port(port) == 0u) {
    return APP_EINVAL;
  }
  if (s_qos_initialized == 0u) {
    (void)lan9645_qos_init();
  }

  st = lan9645_qos_rmw_verify_mask(
    core,
    &LAN9645_REG_ANA_QOS_CFG,
    (uint32_t)port,
    0u,
    mask,
    value
  );
  if (st != APP_OK) {
    lan9645_qos_report_fail(
      port,
      lan9645_reg_desc_rel_addr(&LAN9645_REG_ANA_QOS_CFG, (uint32_t)port, 0u),
      st,
      3u
    );
    return st;
  }

  return APP_OK;
}

/**
 * @brief Sets per-port default QoS values with readback verification.
 */
app_status_t lan9645_qos_set_port_default(
  lan9645_core_t *core,
  uint8_t port,
  uint8_t default_prio,
  uint8_t default_dp
)
{
  const uint32_t mask =
    LAN9645_ANA_QOS_CFG_DP_DEFAULT_VAL |
    LAN9645_ANA_QOS_CFG_QOS_DEFAULT_VAL;
  const uint32_t value =
    LAN9645_ANA_QOS_CFG_DP_DEFAULT_VAL_SET(default_dp) |
    LAN9645_ANA_QOS_CFG_QOS_DEFAULT_VAL_SET(default_prio);
  app_status_t st = lan9645_qos_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if ((lan9645_qos_is_managed_port(port) == 0u) ||
      (default_prio > LAN9645_QOS_DEFAULT_PRIO_MAX) ||
      (default_dp > LAN9645_QOS_DEFAULT_DP_MAX)) {
    return APP_EINVAL;
  }
  if (s_qos_initialized == 0u) {
    (void)lan9645_qos_init();
  }

  st = lan9645_qos_rmw_verify_mask(
    core,
    &LAN9645_REG_ANA_QOS_CFG,
    (uint32_t)port,
    0u,
    mask,
    value
  );
  if (st != APP_OK) {
    lan9645_qos_report_fail(
      port,
      lan9645_reg_desc_rel_addr(&LAN9645_REG_ANA_QOS_CFG, (uint32_t)port, 0u),
      st,
      4u
    );
    return st;
  }

  return APP_OK;
}

/**
 * @brief Reads per-port QoS trust/default policy snapshot.
 */
app_status_t lan9645_qos_get_port_cfg(
  lan9645_core_t *core,
  uint8_t port,
  lan9645_qos_port_cfg_t *out
)
{
  uint32_t rd = 0u;
  app_status_t st = lan9645_qos_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if ((out == NULL) || (lan9645_qos_is_managed_port(port) == 0u)) {
    return APP_EINVAL;
  }

  st = lan9645_qos_read_port_qos_cfg(core, port, &rd);
  if (st != APP_OK) {
    return st;
  }

  memset(out, 0, sizeof(*out));
  out->port = port;
  out->trust_pcp = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_QOS_CFG_QOS_PCP_ENA, rd);
  out->trust_dscp = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_QOS_CFG_QOS_DSCP_ENA, rd);
  out->default_prio = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_QOS_CFG_QOS_DEFAULT_VAL, rd);
  out->default_dp = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_QOS_CFG_DP_DEFAULT_VAL, rd);
  return APP_OK;
}

/**
 * @brief Sets one global DSCP-map entry with readback verification.
 */
app_status_t lan9645_qos_set_dscp_entry(
  lan9645_core_t *core,
  uint8_t dscp,
  uint8_t qos_prio,
  uint8_t dp,
  uint8_t trust_enable,
  uint8_t rewrite_enable
)
{
  const uint32_t mask =
    LAN9645_ANA_DSCP_CFG_DP_DSCP_VAL |
    LAN9645_ANA_DSCP_CFG_QOS_DSCP_VAL |
    LAN9645_ANA_DSCP_CFG_DSCP_TRUST_ENA |
    LAN9645_ANA_DSCP_CFG_DSCP_REWR_ENA;
  const uint32_t value =
    LAN9645_ANA_DSCP_CFG_DP_DSCP_VAL_SET(dp) |
    LAN9645_ANA_DSCP_CFG_QOS_DSCP_VAL_SET(qos_prio) |
    LAN9645_ANA_DSCP_CFG_DSCP_TRUST_ENA_SET((trust_enable != 0u) ? 1u : 0u) |
    LAN9645_ANA_DSCP_CFG_DSCP_REWR_ENA_SET((rewrite_enable != 0u) ? 1u : 0u);
  app_status_t st = lan9645_qos_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if ((dscp > LAN9645_QOS_DSCP_MAX) ||
      (qos_prio > LAN9645_QOS_DEFAULT_PRIO_MAX) ||
      (dp > LAN9645_QOS_DEFAULT_DP_MAX)) {
    return APP_EINVAL;
  }
  if (s_qos_initialized == 0u) {
    (void)lan9645_qos_init();
  }

  st = lan9645_qos_rmw_verify_mask(
    core,
    &LAN9645_REG_ANA_DSCP_CFG,
    0u,
    (uint32_t)dscp,
    mask,
    value
  );
  if (st != APP_OK) {
    lan9645_qos_report_fail(
      dscp,
      lan9645_reg_desc_rel_addr(&LAN9645_REG_ANA_DSCP_CFG, 0u, (uint32_t)dscp),
      st,
      5u
    );
    return st;
  }

  return APP_OK;
}

/**
 * @brief Reads one global DSCP-map entry.
 */
app_status_t lan9645_qos_get_dscp_entry(
  lan9645_core_t *core,
  uint8_t dscp,
  lan9645_qos_dscp_cfg_t *out
)
{
  uint32_t rd = 0u;
  app_status_t st = lan9645_qos_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if ((out == NULL) || (dscp > LAN9645_QOS_DSCP_MAX)) {
    return APP_EINVAL;
  }

  st = lan9645_reg_read32_desc(core->reg, &LAN9645_REG_ANA_DSCP_CFG, 0u, 0u, (uint32_t)dscp, &rd);
  if (st != APP_OK) {
    return st;
  }

  memset(out, 0, sizeof(*out));
  out->dscp = dscp;
  out->qos_prio = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_DSCP_CFG_QOS_DSCP_VAL, rd);
  out->dp = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_DSCP_CFG_DP_DSCP_VAL, rd);
  out->trust_enable = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_DSCP_CFG_DSCP_TRUST_ENA, rd);
  out->rewrite_enable = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_DSCP_CFG_DSCP_REWR_ENA, rd);
  return APP_OK;
}

/**
 * @brief Sets one per-port PCP/DEI entry for ingress and rewrite maps.
 */
app_status_t lan9645_qos_set_pcpdei_entry(
  lan9645_core_t *core,
  uint8_t port,
  uint8_t pcp,
  uint8_t dei,
  uint8_t qos_prio,
  uint8_t dp,
  uint8_t rew_pcp,
  uint8_t rew_dei
)
{
  const uint32_t idx = (LAN9645_QOS_PRIO_COUNT * (uint32_t)dei) + (uint32_t)pcp;
  const uint32_t ana_mask =
    LAN9645_ANA_PCP_DEI_CFG_DP_PCP_DEI_VAL |
    LAN9645_ANA_PCP_DEI_CFG_QOS_PCP_DEI_VAL;
  const uint32_t ana_value =
    LAN9645_ANA_PCP_DEI_CFG_QOS_PCP_DEI_VAL_SET(qos_prio) |
    LAN9645_ANA_PCP_DEI_CFG_DP_PCP_DEI_VAL_SET(dp);
  const uint32_t rew_mask =
    LAN9645_REW_PCP_DEI_CFG_DEI_QOS_VAL |
    LAN9645_REW_PCP_DEI_CFG_PCP_QOS_VAL;
  const uint32_t rew_value =
    LAN9645_REW_PCP_DEI_CFG_PCP_QOS_VAL_SET(rew_pcp) |
    LAN9645_REW_PCP_DEI_CFG_DEI_QOS_VAL_SET(rew_dei);
  app_status_t st = lan9645_qos_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if ((lan9645_qos_is_managed_port(port) == 0u) ||
      (pcp > LAN9645_QOS_PCP_MAX) ||
      (dei > LAN9645_QOS_DEI_MAX) ||
      (qos_prio > LAN9645_QOS_DEFAULT_PRIO_MAX) ||
      (dp > LAN9645_QOS_DEFAULT_DP_MAX) ||
      (rew_pcp > LAN9645_QOS_PCP_MAX) ||
      (rew_dei > LAN9645_QOS_DEI_MAX)) {
    return APP_EINVAL;
  }
  if (s_qos_initialized == 0u) {
    (void)lan9645_qos_init();
  }

  st = lan9645_qos_write_verify_mask(
    core,
    &LAN9645_REG_ANA_PCP_DEI_CFG,
    (uint32_t)port,
    idx,
    ana_value,
    ana_mask
  );
  if (st != APP_OK) {
    lan9645_qos_report_fail(
      port,
      lan9645_reg_desc_rel_addr(&LAN9645_REG_ANA_PCP_DEI_CFG, (uint32_t)port, idx),
      st,
      6u
    );
    return st;
  }

  st = lan9645_qos_write_verify_mask(
    core,
    &LAN9645_REG_REW_PCP_DEI_CFG,
    (uint32_t)port,
    idx,
    rew_value,
    rew_mask
  );
  if (st != APP_OK) {
    lan9645_qos_report_fail(
      port,
      lan9645_reg_desc_rel_addr(&LAN9645_REG_REW_PCP_DEI_CFG, (uint32_t)port, idx),
      st,
      7u
    );
    return st;
  }

  return APP_OK;
}

/**
 * @brief Reads one per-port PCP/DEI entry for ingress and rewrite maps.
 */
app_status_t lan9645_qos_get_pcpdei_entry(
  lan9645_core_t *core,
  uint8_t port,
  uint8_t pcp,
  uint8_t dei,
  lan9645_qos_pcpdei_cfg_t *out
)
{
  uint32_t rd = 0u;
  uint32_t rew = 0u;
  uint32_t idx = 0u;
  app_status_t st = lan9645_qos_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if ((out == NULL) ||
      (lan9645_qos_is_managed_port(port) == 0u) ||
      (pcp > LAN9645_QOS_PCP_MAX) ||
      (dei > LAN9645_QOS_DEI_MAX)) {
    return APP_EINVAL;
  }

  idx = (LAN9645_QOS_PRIO_COUNT * (uint32_t)dei) + (uint32_t)pcp;

  st = lan9645_reg_read32_desc(core->reg, &LAN9645_REG_ANA_PCP_DEI_CFG, 0u, (uint32_t)port, idx, &rd);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_reg_read32_desc(core->reg, &LAN9645_REG_REW_PCP_DEI_CFG, 0u, (uint32_t)port, idx, &rew);
  if (st != APP_OK) {
    return st;
  }

  memset(out, 0, sizeof(*out));
  out->port = port;
  out->pcp = pcp;
  out->dei = dei;
  out->qos_prio = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_PCP_DEI_CFG_QOS_PCP_DEI_VAL, rd);
  out->dp = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_PCP_DEI_CFG_DP_PCP_DEI_VAL, rd);
  out->rew_pcp = (uint8_t)LAN9645_FIELD_GET(LAN9645_REW_PCP_DEI_CFG_PCP_QOS_VAL, rew);
  out->rew_dei = (uint8_t)LAN9645_FIELD_GET(LAN9645_REW_PCP_DEI_CFG_DEI_QOS_VAL, rew);
  return APP_OK;
}

/**
 * @brief Sets per-port rewrite-tag policy with readback verification.
 */
app_status_t lan9645_qos_set_rew_tag_cfg(
  lan9645_core_t *core,
  uint8_t port,
  uint8_t tag_pcp_cfg,
  uint8_t tag_dei_cfg
)
{
  const uint32_t mask =
    LAN9645_REW_TAG_CFG_TAG_PCP_CFG |
    LAN9645_REW_TAG_CFG_TAG_DEI_CFG;
  const uint32_t value =
    LAN9645_REW_TAG_CFG_TAG_PCP_CFG_SET(tag_pcp_cfg) |
    LAN9645_REW_TAG_CFG_TAG_DEI_CFG_SET(tag_dei_cfg);
  app_status_t st = lan9645_qos_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if ((lan9645_qos_is_managed_port(port) == 0u) ||
      (tag_pcp_cfg > LAN9645_QOS_REW_TAG_CFG_MAX) ||
      (tag_dei_cfg > LAN9645_QOS_REW_TAG_CFG_MAX)) {
    return APP_EINVAL;
  }
  if (s_qos_initialized == 0u) {
    (void)lan9645_qos_init();
  }

  st = lan9645_qos_rmw_verify_mask(
    core,
    &LAN9645_REG_REW_TAG_CFG,
    (uint32_t)port,
    0u,
    mask,
    value
  );
  if (st != APP_OK) {
    lan9645_qos_report_fail(
      port,
      lan9645_reg_desc_rel_addr(&LAN9645_REG_REW_TAG_CFG, (uint32_t)port, 0u),
      st,
      8u
    );
    return st;
  }

  return APP_OK;
}

/**
 * @brief Reads per-port rewrite-tag policy snapshot.
 */
app_status_t lan9645_qos_get_rew_tag_cfg(
  lan9645_core_t *core,
  uint8_t port,
  lan9645_qos_rew_tag_cfg_t *out
)
{
  uint32_t rd = 0u;
  app_status_t st = lan9645_qos_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if ((out == NULL) || (lan9645_qos_is_managed_port(port) == 0u)) {
    return APP_EINVAL;
  }

  st = lan9645_reg_read32_desc(core->reg, &LAN9645_REG_REW_TAG_CFG, 0u, (uint32_t)port, 0u, &rd);
  if (st != APP_OK) {
    return st;
  }

  memset(out, 0, sizeof(*out));
  out->port = port;
  out->tag_cfg = (uint8_t)LAN9645_FIELD_GET(LAN9645_REW_TAG_CFG_TAG_CFG, rd);
  out->tag_pcp_cfg = (uint8_t)LAN9645_FIELD_GET(LAN9645_REW_TAG_CFG_TAG_PCP_CFG, rd);
  out->tag_dei_cfg = (uint8_t)LAN9645_FIELD_GET(LAN9645_REW_TAG_CFG_TAG_DEI_CFG, rd);
  return APP_OK;
}

/**
 * @brief Sets per-port rewrite tag insertion mode with readback verification.
 */
app_status_t lan9645_qos_set_rew_tag_mode(
  lan9645_core_t *core,
  uint8_t port,
  uint8_t tag_cfg
)
{
  const uint32_t mask = LAN9645_REW_TAG_CFG_TAG_CFG;
  const uint32_t value = LAN9645_REW_TAG_CFG_TAG_CFG_SET(tag_cfg);
  app_status_t st = lan9645_qos_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if ((lan9645_qos_is_managed_port(port) == 0u) ||
      (tag_cfg > LAN9645_QOS_REW_TAG_MODE_MAX)) {
    return APP_EINVAL;
  }
  if (s_qos_initialized == 0u) {
    (void)lan9645_qos_init();
  }

  st = lan9645_qos_rmw_verify_mask(
    core,
    &LAN9645_REG_REW_TAG_CFG,
    (uint32_t)port,
    0u,
    mask,
    value
  );
  if (st != APP_OK) {
    lan9645_qos_report_fail(
      port,
      lan9645_reg_desc_rel_addr(&LAN9645_REG_REW_TAG_CFG, (uint32_t)port, 0u),
      st,
      10u
    );
    return st;
  }

  return APP_OK;
}

/**
 * @brief Sets per-port rewrite default VLAN PCP/DEI with readback verification.
 */
app_status_t lan9645_qos_set_rew_port_cfg(
  lan9645_core_t *core,
  uint8_t port,
  uint8_t port_pcp,
  uint8_t port_dei
)
{
  const uint32_t mask =
    LAN9645_REW_PORT_VLAN_CFG_PORT_DEI |
    LAN9645_REW_PORT_VLAN_CFG_PORT_PCP;
  const uint32_t value =
    LAN9645_REW_PORT_VLAN_CFG_PORT_DEI_SET(port_dei) |
    LAN9645_REW_PORT_VLAN_CFG_PORT_PCP_SET(port_pcp);
  app_status_t st = lan9645_qos_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if ((lan9645_qos_is_managed_port(port) == 0u) ||
      (port_pcp > LAN9645_QOS_PCP_MAX) ||
      (port_dei > LAN9645_QOS_DEI_MAX)) {
    return APP_EINVAL;
  }
  if (s_qos_initialized == 0u) {
    (void)lan9645_qos_init();
  }

  st = lan9645_qos_rmw_verify_mask(
    core,
    &LAN9645_REG_REW_PORT_VLAN_CFG,
    (uint32_t)port,
    0u,
    mask,
    value
  );
  if (st != APP_OK) {
    lan9645_qos_report_fail(
      port,
      lan9645_reg_desc_rel_addr(&LAN9645_REG_REW_PORT_VLAN_CFG, (uint32_t)port, 0u),
      st,
      9u
    );
    return st;
  }

  return APP_OK;
}

/**
 * @brief Reads per-port rewrite default VLAN PCP/DEI snapshot.
 */
app_status_t lan9645_qos_get_rew_port_cfg(
  lan9645_core_t *core,
  uint8_t port,
  lan9645_qos_rew_port_cfg_t *out
)
{
  uint32_t rd = 0u;
  app_status_t st = lan9645_qos_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if ((out == NULL) || (lan9645_qos_is_managed_port(port) == 0u)) {
    return APP_EINVAL;
  }

  st = lan9645_reg_read32_desc(core->reg, &LAN9645_REG_REW_PORT_VLAN_CFG, 0u, (uint32_t)port, 0u, &rd);
  if (st != APP_OK) {
    return st;
  }

  memset(out, 0, sizeof(*out));
  out->port = port;
  out->port_pcp = (uint8_t)LAN9645_FIELD_GET(LAN9645_REW_PORT_VLAN_CFG_PORT_PCP, rd);
  out->port_dei = (uint8_t)LAN9645_FIELD_GET(LAN9645_REW_PORT_VLAN_CFG_PORT_DEI, rd);
  return APP_OK;
}
