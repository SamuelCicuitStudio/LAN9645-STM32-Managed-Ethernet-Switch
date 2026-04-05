#include "lan9645/lan9645_vcap.h"

/**
 * @file lan9645_vcap.c
 * @brief Baseline VCAP-domain configuration aligned with Linux LAN9645 behavior.
 */

#include "lan9645/lan9645_reg.h"
#include "lan9645/lan9645_regs.h"

static const uint8_t s_vcap_ports[] = {0u, 1u, 8u};
static uint8_t s_vcap_initialized = 0u;

/**
 * @brief Validates that VCAP operations can access a ready core/register layer.
 */
static app_status_t lan9645_vcap_validate_core(const lan9645_core_t *core)
{
  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }

  return APP_OK;
}

/**
 * @brief Validates one managed port index accepted by the VCAP domain.
 */
static app_status_t lan9645_vcap_validate_port(uint8_t port)
{
  for (uint32_t i = 0u; i < (uint32_t)(sizeof(s_vcap_ports) / sizeof(s_vcap_ports[0])); ++i) {
    if (s_vcap_ports[i] == port) {
      return APP_OK;
    }
  }

  return APP_EINVAL;
}

/**
 * @brief Initializes VCAP baseline service state.
 */
app_status_t lan9645_vcap_init(void)
{
  s_vcap_initialized = 1u;
  return APP_OK;
}

/**
 * @brief Reads one managed-port VCAP S1/S2 configuration snapshot.
 */
app_status_t lan9645_vcap_get_port_cfg(
  lan9645_core_t *core,
  uint8_t port,
  lan9645_vcap_port_cfg_t *out
)
{
  uint32_t s1 = 0u;
  uint32_t s2 = 0u;
  app_status_t st = lan9645_vcap_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_vcap_validate_port(port);
  if (st != APP_OK) {
    return st;
  }
  if (out == NULL) {
    return APP_EINVAL;
  }

  st = lan9645_reg_read32_desc(core->reg, &LAN9645_REG_ANA_VCAP_CFG, 0u, (uint32_t)port, 0u, &s1);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_reg_read32_desc(core->reg, &LAN9645_REG_ANA_VCAP_S2_CFG, 0u, (uint32_t)port, 0u, &s2);
  if (st != APP_OK) {
    return st;
  }

  out->port = port;
  out->s1_smac_ena = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_VCAP_CFG_S1_SMAC_ENA, s1);
  out->s1_ena = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_VCAP_CFG_S1_ENA, s1);
  out->s1_dmac_dip_ena = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_VCAP_CFG_S1_DMAC_DIP_ENA, s1);
  out->s1_vlan_inner_tag_ena = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_VCAP_CFG_S1_VLAN_INNER_TAG_ENA, s1);
  out->s1_pag_val = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_VCAP_CFG_PAG_VAL, s1);
  out->s2_isdx_ena = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_VCAP_S2_CFG_ISDX_ENA, s2);
  out->s2_udp_payload_ena = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_VCAP_S2_CFG_UDP_PAYLOAD_ENA, s2);
  out->s2_etype_payload_ena = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_VCAP_S2_CFG_ETYPE_PAYLOAD_ENA, s2);
  out->s2_ena = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_VCAP_S2_CFG_ENA, s2);
  out->s2_snap_dis = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_VCAP_S2_CFG_SNAP_DIS, s2);
  out->s2_arp_dis = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_VCAP_S2_CFG_ARP_DIS, s2);
  out->s2_ip_tcpudp_dis = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_VCAP_S2_CFG_IP_TCPUDP_DIS, s2);
  out->s2_ip_other_dis = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_VCAP_S2_CFG_IP_OTHER_DIS, s2);
  out->s2_ip6_cfg = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_VCAP_S2_CFG_IP6_CFG, s2);
  out->s2_oam_dis = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_VCAP_S2_CFG_OAM_DIS, s2);
  out->s1_raw = s1;
  out->s2_raw = s2;
  return APP_OK;
}

/**
 * @brief Writes one managed-port VCAP S1/S2 configuration with readback verification.
 */
app_status_t lan9645_vcap_set_port_cfg(
  lan9645_core_t *core,
  const lan9645_vcap_port_cfg_t *cfg
)
{
  const uint32_t s1_mask =
    LAN9645_ANA_VCAP_CFG_S1_SMAC_ENA |
    LAN9645_ANA_VCAP_CFG_S1_ENA |
    LAN9645_ANA_VCAP_CFG_S1_DMAC_DIP_ENA |
    LAN9645_ANA_VCAP_CFG_S1_VLAN_INNER_TAG_ENA |
    LAN9645_ANA_VCAP_CFG_PAG_VAL;
  const uint32_t s1_value =
    LAN9645_ANA_VCAP_CFG_S1_SMAC_ENA_SET((uint32_t)cfg->s1_smac_ena) |
    LAN9645_ANA_VCAP_CFG_S1_ENA_SET((uint32_t)cfg->s1_ena) |
    LAN9645_ANA_VCAP_CFG_S1_DMAC_DIP_ENA_SET((uint32_t)cfg->s1_dmac_dip_ena) |
    LAN9645_ANA_VCAP_CFG_S1_VLAN_INNER_TAG_ENA_SET((uint32_t)cfg->s1_vlan_inner_tag_ena) |
    LAN9645_ANA_VCAP_CFG_PAG_VAL_SET((uint32_t)cfg->s1_pag_val);
  const uint32_t s2_mask =
    LAN9645_ANA_VCAP_S2_CFG_ISDX_ENA |
    LAN9645_ANA_VCAP_S2_CFG_UDP_PAYLOAD_ENA |
    LAN9645_ANA_VCAP_S2_CFG_ETYPE_PAYLOAD_ENA |
    LAN9645_ANA_VCAP_S2_CFG_ENA |
    LAN9645_ANA_VCAP_S2_CFG_SNAP_DIS |
    LAN9645_ANA_VCAP_S2_CFG_ARP_DIS |
    LAN9645_ANA_VCAP_S2_CFG_IP_TCPUDP_DIS |
    LAN9645_ANA_VCAP_S2_CFG_IP_OTHER_DIS |
    LAN9645_ANA_VCAP_S2_CFG_IP6_CFG |
    LAN9645_ANA_VCAP_S2_CFG_OAM_DIS;
  const uint32_t s2_value =
    LAN9645_ANA_VCAP_S2_CFG_ISDX_ENA_SET((uint32_t)cfg->s2_isdx_ena) |
    LAN9645_ANA_VCAP_S2_CFG_UDP_PAYLOAD_ENA_SET((uint32_t)cfg->s2_udp_payload_ena) |
    LAN9645_ANA_VCAP_S2_CFG_ETYPE_PAYLOAD_ENA_SET((uint32_t)cfg->s2_etype_payload_ena) |
    LAN9645_ANA_VCAP_S2_CFG_ENA_SET((uint32_t)cfg->s2_ena) |
    LAN9645_ANA_VCAP_S2_CFG_SNAP_DIS_SET((uint32_t)cfg->s2_snap_dis) |
    LAN9645_ANA_VCAP_S2_CFG_ARP_DIS_SET((uint32_t)cfg->s2_arp_dis) |
    LAN9645_ANA_VCAP_S2_CFG_IP_TCPUDP_DIS_SET((uint32_t)cfg->s2_ip_tcpudp_dis) |
    LAN9645_ANA_VCAP_S2_CFG_IP_OTHER_DIS_SET((uint32_t)cfg->s2_ip_other_dis) |
    LAN9645_ANA_VCAP_S2_CFG_IP6_CFG_SET((uint32_t)cfg->s2_ip6_cfg) |
    LAN9645_ANA_VCAP_S2_CFG_OAM_DIS_SET((uint32_t)cfg->s2_oam_dis);
  uint32_t rd = 0u;
  app_status_t st = lan9645_vcap_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (cfg == NULL) {
    return APP_EINVAL;
  }
  st = lan9645_vcap_validate_port(cfg->port);
  if (st != APP_OK) {
    return st;
  }
  if ((cfg->s1_smac_ena > LAN9645_VCAP_S1_SMAC_ENA_MAX) ||
      (cfg->s1_ena > 1u) ||
      (cfg->s1_dmac_dip_ena > LAN9645_VCAP_S1_DMAC_DIP_ENA_MAX) ||
      (cfg->s1_vlan_inner_tag_ena > LAN9645_VCAP_S1_VLAN_INNER_TAG_ENA_MAX) ||
      (cfg->s1_pag_val > LAN9645_VCAP_S1_PAG_VAL_MAX) ||
      (cfg->s2_isdx_ena > LAN9645_VCAP_S2_ISDX_ENA_MAX) ||
      (cfg->s2_udp_payload_ena > LAN9645_VCAP_S2_UDP_PAYLOAD_ENA_MAX) ||
      (cfg->s2_etype_payload_ena > LAN9645_VCAP_S2_ETYPE_PAYLOAD_ENA_MAX) ||
      (cfg->s2_ena > 1u) ||
      (cfg->s2_snap_dis > LAN9645_VCAP_S2_SNAP_DIS_MAX) ||
      (cfg->s2_arp_dis > LAN9645_VCAP_S2_ARP_DIS_MAX) ||
      (cfg->s2_ip_tcpudp_dis > LAN9645_VCAP_S2_IP_TCPUDP_DIS_MAX) ||
      (cfg->s2_ip_other_dis > LAN9645_VCAP_S2_IP_OTHER_DIS_MAX) ||
      (cfg->s2_ip6_cfg > LAN9645_VCAP_S2_IP6_CFG_MAX) ||
      (cfg->s2_oam_dis > LAN9645_VCAP_S2_OAM_DIS_MAX)) {
    return APP_EINVAL;
  }

  st = lan9645_reg_rmw32_desc(
    core->reg,
    &LAN9645_REG_ANA_VCAP_CFG,
    0u,
    (uint32_t)cfg->port,
    0u,
    s1_mask,
    s1_value
  );
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_reg_read32_desc(
    core->reg,
    &LAN9645_REG_ANA_VCAP_CFG,
    0u,
    (uint32_t)cfg->port,
    0u,
    &rd
  );
  if (st != APP_OK) {
    return st;
  }
  if ((rd & s1_mask) != (s1_value & s1_mask)) {
    return APP_EIO;
  }

  st = lan9645_reg_rmw32_desc(
    core->reg,
    &LAN9645_REG_ANA_VCAP_S2_CFG,
    0u,
    (uint32_t)cfg->port,
    0u,
    s2_mask,
    s2_value
  );
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_reg_read32_desc(
    core->reg,
    &LAN9645_REG_ANA_VCAP_S2_CFG,
    0u,
    (uint32_t)cfg->port,
    0u,
    &rd
  );
  if (st != APP_OK) {
    return st;
  }
  if ((rd & s2_mask) != (s2_value & s2_mask)) {
    return APP_EIO;
  }

  return APP_OK;
}

/**
 * @brief Applies deterministic VCAP baseline.
 */
app_status_t lan9645_vcap_apply_baseline(lan9645_core_t *core)
{
  lan9645_vcap_port_cfg_t cfg = {0};
  app_status_t st = lan9645_vcap_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (s_vcap_initialized == 0u) {
    (void)lan9645_vcap_init();
  }

  for (uint32_t i = 0u; i < (uint32_t)(sizeof(s_vcap_ports) / sizeof(s_vcap_ports[0])); ++i) {
    cfg.port = s_vcap_ports[i];
    st = lan9645_vcap_set_port_cfg(core, &cfg);
    if (st != APP_OK) {
      return st;
    }
  }

  return APP_OK;
}
