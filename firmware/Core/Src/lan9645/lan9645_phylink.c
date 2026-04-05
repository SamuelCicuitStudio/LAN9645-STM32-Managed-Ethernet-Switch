#include "lan9645/lan9645_phylink.h"

/**
 * @file lan9645_phylink.c
 * @brief Baseline phylink-domain configuration aligned with LAN9645 RGMII bring-up.
 */

#include "lan9645/lan9645_reg.h"
#include "lan9645/lan9645_regs.h"

#define LAN9645_PHYLINK_CPU_PORT     (8u)
#define LAN9645_PHYLINK_RGMII_IDX    (1u)
#define LAN9645_PHYLINK_MAX_CPU_PORT (8u)

static uint8_t s_phylink_initialized = 0u;

/**
 * @brief Validates that phylink operations can access a ready core/register layer.
 */
static app_status_t lan9645_phylink_validate_core(const lan9645_core_t *core)
{
  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }

  return APP_OK;
}

/**
 * @brief Validates one RGMII interface index.
 */
static app_status_t lan9645_phylink_validate_rgmii_idx(uint8_t rgmii_idx)
{
  if (rgmii_idx > LAN9645_PHYLINK_RGMII_IDX_MAX) {
    return APP_EINVAL;
  }

  return APP_OK;
}

/**
 * @brief Validates one CPU port index used by phylink routines.
 */
static app_status_t lan9645_phylink_validate_cpu_port(uint8_t cpu_port)
{
  if (cpu_port > LAN9645_PHYLINK_MAX_CPU_PORT) {
    return APP_EINVAL;
  }

  return APP_OK;
}

/**
 * @brief Reads one RGMII interface configuration/status snapshot.
 */
app_status_t lan9645_phylink_get_rgmii_cfg(
  lan9645_core_t *core,
  uint8_t rgmii_idx,
  lan9645_phylink_rgmii_cfg_t *out
)
{
  uint32_t rd = 0u;
  app_status_t st = lan9645_phylink_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_phylink_validate_rgmii_idx(rgmii_idx);
  if (st != APP_OK) {
    return st;
  }
  if (out == NULL) {
    return APP_EINVAL;
  }

  st = lan9645_reg_read32_desc(
    core->reg,
    &LAN9645_REG_HSIO_RGMII_CFG,
    0u,
    0u,
    (uint32_t)rgmii_idx,
    &rd
  );
  if (st != APP_OK) {
    return st;
  }

  out->rgmii_idx = rgmii_idx;
  out->ib_enable = (uint8_t)LAN9645_FIELD_GET(LAN9645_HSIO_RGMII_CFG_IB_ENA, rd);
  out->ib_rx_enable = (uint8_t)LAN9645_FIELD_GET(LAN9645_HSIO_RGMII_CFG_IB_RX_ENA, rd);
  out->ib_tx_enable = (uint8_t)LAN9645_FIELD_GET(LAN9645_HSIO_RGMII_CFG_IB_TX_ENA, rd);
  out->rgmii_rx_rst = (uint8_t)LAN9645_FIELD_GET(LAN9645_HSIO_RGMII_CFG_RGMII_RX_RST, rd);
  out->rgmii_tx_rst = (uint8_t)LAN9645_FIELD_GET(LAN9645_HSIO_RGMII_CFG_RGMII_TX_RST, rd);
  out->tx_clk_cfg = (uint8_t)LAN9645_FIELD_GET(LAN9645_HSIO_RGMII_CFG_TX_CLK_CFG, rd);
  out->link_up = (uint8_t)LAN9645_FIELD_GET(LAN9645_HSIO_RGMII_CFG_IB_RX_LINK_STATUS, rd);
  out->duplex = (uint8_t)LAN9645_FIELD_GET(LAN9645_HSIO_RGMII_CFG_IB_RX_DUPLEX, rd);
  out->speed_code = (uint8_t)LAN9645_FIELD_GET(LAN9645_HSIO_RGMII_CFG_IB_RX_SPEED, rd);
  return APP_OK;
}

/**
 * @brief Writes one RGMII interface configuration with readback verification.
 */
app_status_t lan9645_phylink_set_rgmii_cfg(
  lan9645_core_t *core,
  uint8_t rgmii_idx,
  uint8_t ib_enable,
  uint8_t ib_rx_enable,
  uint8_t ib_tx_enable,
  uint8_t tx_clk_cfg,
  uint8_t rgmii_rx_rst,
  uint8_t rgmii_tx_rst
)
{
  const uint32_t mask =
    LAN9645_HSIO_RGMII_CFG_IB_TX_ENA |
    LAN9645_HSIO_RGMII_CFG_IB_RX_ENA |
    LAN9645_HSIO_RGMII_CFG_IB_ENA |
    LAN9645_HSIO_RGMII_CFG_TX_CLK_CFG |
    LAN9645_HSIO_RGMII_CFG_RGMII_TX_RST |
    LAN9645_HSIO_RGMII_CFG_RGMII_RX_RST;
  const uint32_t value =
    LAN9645_HSIO_RGMII_CFG_IB_TX_ENA * (uint32_t)(ib_tx_enable != 0u) |
    LAN9645_HSIO_RGMII_CFG_IB_RX_ENA * (uint32_t)(ib_rx_enable != 0u) |
    LAN9645_HSIO_RGMII_CFG_IB_ENA * (uint32_t)(ib_enable != 0u) |
    LAN9645_HSIO_RGMII_CFG_TX_CLK_CFG_SET((uint32_t)tx_clk_cfg) |
    LAN9645_HSIO_RGMII_CFG_RGMII_TX_RST * (uint32_t)(rgmii_tx_rst != 0u) |
    LAN9645_HSIO_RGMII_CFG_RGMII_RX_RST * (uint32_t)(rgmii_rx_rst != 0u);
  uint32_t rd = 0u;
  app_status_t st = lan9645_phylink_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_phylink_validate_rgmii_idx(rgmii_idx);
  if (st != APP_OK) {
    return st;
  }
  if ((ib_enable > 1u) ||
      (ib_rx_enable > 1u) ||
      (ib_tx_enable > 1u) ||
      (tx_clk_cfg > LAN9645_PHYLINK_TX_CLK_CFG_MAX) ||
      (rgmii_rx_rst > 1u) ||
      (rgmii_tx_rst > 1u)) {
    return APP_EINVAL;
  }

  st = lan9645_reg_rmw32_desc(
    core->reg,
    &LAN9645_REG_HSIO_RGMII_CFG,
    0u,
    0u,
    (uint32_t)rgmii_idx,
    mask,
    value
  );
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_reg_read32_desc(
    core->reg,
    &LAN9645_REG_HSIO_RGMII_CFG,
    0u,
    0u,
    (uint32_t)rgmii_idx,
    &rd
  );
  if (st != APP_OK) {
    return st;
  }
  if ((rd & mask) != (value & mask)) {
    return APP_EIO;
  }

  return APP_OK;
}

/**
 * @brief Reads one CPU-port MAC/admin snapshot used by phylink control.
 */
app_status_t lan9645_phylink_get_cpu_cfg(
  lan9645_core_t *core,
  uint8_t cpu_port,
  lan9645_phylink_cpu_cfg_t *out
)
{
  lan9645_core_port_cfg_t cfg = {0};
  app_status_t st = lan9645_phylink_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_phylink_validate_cpu_port(cpu_port);
  if (st != APP_OK) {
    return st;
  }
  if (out == NULL) {
    return APP_EINVAL;
  }

  st = lan9645_core_get_port_cfg(core, cpu_port, &cfg);
  if (st != APP_OK) {
    return st;
  }

  out->port = cpu_port;
  out->admin_up = cfg.admin_up;
  out->speed_mbps = cfg.speed_mbps;
  out->full_duplex = cfg.full_duplex;
  out->mac_rx_ena = cfg.mac_rx_ena;
  out->mac_tx_ena = cfg.mac_tx_ena;
  return APP_OK;
}

/**
 * @brief Writes one CPU-port MAC/admin configuration used by phylink control.
 */
app_status_t lan9645_phylink_set_cpu_cfg(
  lan9645_core_t *core,
  uint8_t cpu_port,
  uint8_t admin_up,
  uint16_t speed_mbps,
  uint8_t full_duplex
)
{
  lan9645_core_port_cfg_t cfg = {0};
  app_status_t st = lan9645_phylink_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_phylink_validate_cpu_port(cpu_port);
  if (st != APP_OK) {
    return st;
  }
  if ((admin_up > 1u) ||
      (full_duplex > 1u) ||
      ((speed_mbps != LAN9645_CORE_PORT_SPEED_10_MBPS) &&
       (speed_mbps != LAN9645_CORE_PORT_SPEED_100_MBPS))) {
    return APP_EINVAL;
  }

  st = lan9645_core_set_port_mode(core, cpu_port, speed_mbps, full_duplex);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_core_set_port_admin(core, cpu_port, admin_up);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_core_get_port_cfg(core, cpu_port, &cfg);
  if (st != APP_OK) {
    return st;
  }
  if ((cfg.admin_up != admin_up) ||
      (cfg.speed_mbps != speed_mbps) ||
      (cfg.full_duplex != full_duplex) ||
      (cfg.mac_rx_ena == 0u) ||
      (cfg.mac_tx_ena == 0u)) {
    return APP_EIO;
  }

  return APP_OK;
}

/**
 * @brief Initializes phylink baseline service state.
 */
app_status_t lan9645_phylink_init(void)
{
  s_phylink_initialized = 1u;
  return APP_OK;
}

/**
 * @brief Applies deterministic phylink baseline.
 */
app_status_t lan9645_phylink_apply_baseline(lan9645_core_t *core)
{
  lan9645_phylink_rgmii_cfg_t rgmii = {0};
  app_status_t st = lan9645_phylink_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (s_phylink_initialized == 0u) {
    (void)lan9645_phylink_init();
  }

  st = lan9645_phylink_set_cpu_cfg(
    core,
    LAN9645_PHYLINK_CPU_PORT,
    1u,
    LAN9645_CORE_PORT_SPEED_100_MBPS,
    1u
  );
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_phylink_get_rgmii_cfg(core, LAN9645_PHYLINK_RGMII_IDX, &rgmii);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_phylink_set_rgmii_cfg(
    core,
    LAN9645_PHYLINK_RGMII_IDX,
    1u,
    1u,
    1u,
    rgmii.tx_clk_cfg,
    0u,
    0u
  );
  if (st != APP_OK) {
    return st;
  }

  return lan9645_phylink_get_rgmii_cfg(core, LAN9645_PHYLINK_RGMII_IDX, &rgmii);
}
