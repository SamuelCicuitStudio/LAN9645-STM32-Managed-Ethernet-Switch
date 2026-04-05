#include "lan9645/lan9645_mac.h"

/**
 * @file lan9645_mac.c
 * @brief Baseline MAC-domain configuration for managed LAN9645 ports.
 */

static const uint8_t s_mac_ports[] = {0u, 1u, 8u};
static uint8_t s_mac_initialized = 0u;
static const uint8_t s_mac_port_max = 8u;

/**
 * @brief Validates that MAC operations can access a ready core/register layer.
 */
static app_status_t lan9645_mac_validate_core(const lan9645_core_t *core)
{
  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }

  return APP_OK;
}

/**
 * @brief Validates one MAC managed port index.
 */
static app_status_t lan9645_mac_validate_port(uint8_t port)
{
  if (port > s_mac_port_max) {
    return APP_EINVAL;
  }

  return APP_OK;
}

/**
 * @brief Reads one MAC per-port configuration snapshot.
 */
app_status_t lan9645_mac_get_port_cfg(
  lan9645_core_t *core,
  uint8_t port,
  lan9645_mac_port_cfg_t *out
)
{
  lan9645_core_port_cfg_t cfg = {0};
  app_status_t st = lan9645_mac_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_mac_validate_port(port);
  if (st != APP_OK) {
    return st;
  }
  if (out == NULL) {
    return APP_EINVAL;
  }

  st = lan9645_core_get_port_cfg(core, port, &cfg);
  if (st != APP_OK) {
    return st;
  }

  out->port = port;
  out->admin_up = cfg.admin_up;
  out->speed_mbps = cfg.speed_mbps;
  out->full_duplex = cfg.full_duplex;
  out->mac_rx_ena = cfg.mac_rx_ena;
  out->mac_tx_ena = cfg.mac_tx_ena;
  return APP_OK;
}

/**
 * @brief Writes one MAC per-port configuration and verifies readback.
 */
app_status_t lan9645_mac_set_port_cfg(
  lan9645_core_t *core,
  uint8_t port,
  uint8_t admin_up,
  uint16_t speed_mbps,
  uint8_t full_duplex
)
{
  lan9645_core_port_cfg_t cfg = {0};
  app_status_t st = lan9645_mac_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_mac_validate_port(port);
  if (st != APP_OK) {
    return st;
  }
  if ((admin_up > 1u) || (full_duplex > 1u) ||
      ((speed_mbps != LAN9645_CORE_PORT_SPEED_10_MBPS) &&
       (speed_mbps != LAN9645_CORE_PORT_SPEED_100_MBPS))) {
    return APP_EINVAL;
  }

  st = lan9645_core_set_port_mode(core, port, speed_mbps, full_duplex);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_core_set_port_admin(core, port, admin_up);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_core_get_port_cfg(core, port, &cfg);
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
 * @brief Applies MAC mode/admin baseline on one managed port and verifies readback.
 */
static app_status_t lan9645_mac_apply_port(lan9645_core_t *core, uint8_t port)
{
  return lan9645_mac_set_port_cfg(core, port, 1u, LAN9645_CORE_PORT_SPEED_100_MBPS, 1u);
}

/**
 * @brief Initializes MAC baseline service state.
 */
app_status_t lan9645_mac_init(void)
{
  s_mac_initialized = 1u;
  return APP_OK;
}

/**
 * @brief Applies deterministic MAC baseline.
 */
app_status_t lan9645_mac_apply_baseline(lan9645_core_t *core)
{
  app_status_t st = lan9645_mac_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (s_mac_initialized == 0u) {
    (void)lan9645_mac_init();
  }

  for (uint32_t i = 0u; i < (uint32_t)(sizeof(s_mac_ports) / sizeof(s_mac_ports[0])); ++i) {
    st = lan9645_mac_apply_port(core, s_mac_ports[i]);
    if (st != APP_OK) {
      return st;
    }
  }

  return APP_OK;
}
