#include "lan9645/lan9645_dcb.h"

/**
 * @file lan9645_dcb.c
 * @brief Baseline DCB configuration aligned with LAN9645 Linux QoS/DCB behavior.
 */

#include "lan9645/lan9645_reg.h"
#include "lan9645/lan9645_regs.h"

static const uint8_t s_dcb_ports[] = {0u, 1u, 8u};
static uint8_t s_dcb_initialized = 0u;
static const uint8_t s_dcb_port_max = 8u;

/**
 * @brief Validates that DCB operations can access an initialized register layer.
 */
static app_status_t lan9645_dcb_validate_core(const lan9645_core_t *core)
{
  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  return APP_OK;
}

/**
 * @brief Validates one DCB port index.
 */
static app_status_t lan9645_dcb_validate_port(uint8_t port)
{
  if (port > s_dcb_port_max) {
    return APP_EINVAL;
  }
  return APP_OK;
}

/**
 * @brief Performs a descriptor RMW and verifies masked readback.
 */
static app_status_t lan9645_dcb_rmw_verify(
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
 * @brief Reads one DCB per-port configuration snapshot.
 */
app_status_t lan9645_dcb_get_port_cfg(
  lan9645_core_t *core,
  uint8_t port,
  lan9645_dcb_port_cfg_t *out
)
{
  uint32_t rd_qos = 0u;
  uint32_t rd_vlan = 0u;
  uint32_t rd_pfc = 0u;
  uint32_t rd_sw = 0u;
  app_status_t st = lan9645_dcb_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_dcb_validate_port(port);
  if (st != APP_OK) {
    return st;
  }
  if (out == NULL) {
    return APP_EINVAL;
  }

  st = lan9645_reg_read32_desc(
    core->reg,
    &LAN9645_REG_ANA_QOS_CFG,
    0u,
    (uint32_t)port,
    0u,
    &rd_qos
  );
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_reg_read32_desc(
    core->reg,
    &LAN9645_REG_ANA_VLAN_CFG,
    0u,
    (uint32_t)port,
    0u,
    &rd_vlan
  );
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_reg_read32_desc(
    core->reg,
    &LAN9645_REG_ANA_PFC_CFG,
    0u,
    (uint32_t)port,
    0u,
    &rd_pfc
  );
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_reg_read32_desc(
    core->reg,
    &LAN9645_REG_QSYS_SW_PORT_MODE,
    0u,
    (uint32_t)port,
    0u,
    &rd_sw
  );
  if (st != APP_OK) {
    return st;
  }

  out->port = port;
  out->trust_pcp = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_QOS_CFG_QOS_PCP_ENA, rd_qos);
  out->trust_dscp = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_QOS_CFG_QOS_DSCP_ENA, rd_qos);
  out->default_prio = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_QOS_CFG_QOS_DEFAULT_VAL, rd_qos);
  out->default_dp = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_QOS_CFG_DP_DEFAULT_VAL, rd_qos);
  out->vlan_pfc_enable = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_VLAN_CFG_VLAN_PFC_ENA, rd_vlan);
  out->rx_pfc_mask = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_PFC_CFG_RX_PFC_ENA, rd_pfc);
  out->tx_pfc_mask = (uint8_t)LAN9645_FIELD_GET(LAN9645_QSYS_SW_PORT_MODE_TX_PFC_ENA, rd_sw);

  return APP_OK;
}

/**
 * @brief Writes one DCB per-port configuration with readback verification.
 */
app_status_t lan9645_dcb_set_port_cfg(
  lan9645_core_t *core,
  uint8_t port,
  uint8_t trust_pcp,
  uint8_t trust_dscp,
  uint8_t default_prio,
  uint8_t default_dp,
  uint8_t vlan_pfc_enable,
  uint8_t rx_pfc_mask,
  uint8_t tx_pfc_mask
)
{
  const uint32_t qos_mask =
    LAN9645_ANA_QOS_CFG_DP_DEFAULT_VAL |
    LAN9645_ANA_QOS_CFG_QOS_DEFAULT_VAL |
    LAN9645_ANA_QOS_CFG_QOS_PCP_ENA |
    LAN9645_ANA_QOS_CFG_QOS_DSCP_ENA;
  const uint32_t qos_value =
    LAN9645_ANA_QOS_CFG_DP_DEFAULT_VAL_SET((uint32_t)default_dp) |
    LAN9645_ANA_QOS_CFG_QOS_DEFAULT_VAL_SET((uint32_t)default_prio) |
    LAN9645_ANA_QOS_CFG_QOS_PCP_ENA_SET((uint32_t)trust_pcp) |
    LAN9645_ANA_QOS_CFG_QOS_DSCP_ENA_SET((uint32_t)trust_dscp);
  app_status_t st = lan9645_dcb_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_dcb_validate_port(port);
  if (st != APP_OK) {
    return st;
  }
  if ((trust_pcp > 1u) ||
      (trust_dscp > 1u) ||
      (default_prio > LAN9645_DCB_DEFAULT_PRIO_MAX) ||
      (default_dp > LAN9645_DCB_DEFAULT_DP_MAX) ||
      (vlan_pfc_enable > 1u) ||
      (rx_pfc_mask > LAN9645_DCB_PFC_MASK_MAX) ||
      (tx_pfc_mask > LAN9645_DCB_PFC_MASK_MAX)) {
    return APP_EINVAL;
  }

  st = lan9645_dcb_rmw_verify(
    core,
    &LAN9645_REG_ANA_QOS_CFG,
    (uint32_t)port,
    0u,
    qos_mask,
    qos_value
  );
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_dcb_rmw_verify(
    core,
    &LAN9645_REG_ANA_VLAN_CFG,
    (uint32_t)port,
    0u,
    LAN9645_ANA_VLAN_CFG_VLAN_PFC_ENA,
    LAN9645_ANA_VLAN_CFG_VLAN_PFC_ENA_SET((uint32_t)vlan_pfc_enable)
  );
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_dcb_rmw_verify(
    core,
    &LAN9645_REG_ANA_PFC_CFG,
    (uint32_t)port,
    0u,
    LAN9645_ANA_PFC_CFG_RX_PFC_ENA,
    LAN9645_ANA_PFC_CFG_RX_PFC_ENA_SET((uint32_t)rx_pfc_mask)
  );
  if (st != APP_OK) {
    return st;
  }

  return lan9645_dcb_rmw_verify(
    core,
    &LAN9645_REG_QSYS_SW_PORT_MODE,
    (uint32_t)port,
    0u,
    LAN9645_QSYS_SW_PORT_MODE_TX_PFC_ENA,
    LAN9645_QSYS_SW_PORT_MODE_TX_PFC_ENA_SET((uint32_t)tx_pfc_mask)
  );
}

/**
 * @brief Applies DCB baseline configuration on one managed port.
 */
static app_status_t lan9645_dcb_apply_port_baseline(lan9645_core_t *core, uint8_t port)
{
  return lan9645_dcb_set_port_cfg(core, port, 1u, 0u, 0u, 0u, 0u, 0u, 0u);
}

/**
 * @brief Initializes DCB baseline service state.
 */
app_status_t lan9645_dcb_init(void)
{
  s_dcb_initialized = 1u;
  return APP_OK;
}

/**
 * @brief Applies deterministic DCB baseline for managed ports.
 */
app_status_t lan9645_dcb_apply_baseline(lan9645_core_t *core)
{
  app_status_t st = lan9645_dcb_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (s_dcb_initialized == 0u) {
    (void)lan9645_dcb_init();
  }

  for (uint32_t i = 0u; i < (uint32_t)(sizeof(s_dcb_ports) / sizeof(s_dcb_ports[0])); ++i) {
    st = lan9645_dcb_apply_port_baseline(core, s_dcb_ports[i]);
    if (st != APP_OK) {
      return st;
    }
  }

  return APP_OK;
}
