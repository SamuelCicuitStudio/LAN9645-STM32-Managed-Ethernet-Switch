#include "cli/cli_core.h"

/**
 * @file cli_core.c
 * @brief Command parser and bring-up control surface for the LAN9645 firmware.
 */

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app/app_orchestrator.h"
#include "cli/cli_ingress.h"
#include "diag/diag_core.h"
#include "diag/diag_router.h"
#include "ethernetif.h"
#include "lan9645/lan9645_profile_managed_rmii1.h"
#include "lan9645/lan9645_reg.h"
#include "lan9645/lan9645_runtime.h"
#include "lan9645/lan9645_user_phy_adapter.h"
#include "lan9645/lan9645_vlan.h"
#include "lwip.h"

#define CLI_MAX_LINE_LEN      (160u)
#define CLI_MAX_TOKENS        (8u)
#define CLI_LAN_CHECK_PORT0    (0u)
#define CLI_LAN_CHECK_PORT1    (1u)
#define CLI_LAN_CHECK_CPU_PORT (8u)
#define CLI_LAN_CHECK_SPEED100 (2u)
#define CLI_LAN_CHECK_CPU_DEV_IRQ_MASK (0x1FFu)

static cli_core_state_t s_cli = {0};
static lan9645_user_phy_adapter_t s_cli_phy = {0};
static uint8_t s_cli_phy_ready = 0u;

/** @brief Forward declaration used by composite bring-up commands. */
static app_status_t cli_cmd_spi_status(void);
/** @brief Forward declaration for SPI transport counters command. */
static app_status_t cli_cmd_spi_stats(void);
/** @brief Forward declaration for SPI transport counters reset command. */
static app_status_t cli_cmd_spi_stats_clear(void);

/** @brief Compares two strings without case sensitivity. */
static uint8_t cli_eq(const char *a, const char *b)
{
  if ((a == NULL) || (b == NULL)) {
    return 0u;
  }

  while ((*a != '\0') && (*b != '\0')) {
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
      return 0u;
    }
    ++a;
    ++b;
  }

  return ((*a == '\0') && (*b == '\0')) ? 1u : 0u;
}

/** @brief Writes a formatted line to the active diagnostics route. */
static app_status_t cli_writef(const char *fmt, ...)
{
  va_list ap;
  char line[220];
  int n = 0;

  if (fmt == NULL) {
    return APP_EINVAL;
  }

  va_start(ap, fmt);
  n = vsnprintf(line, sizeof(line), fmt, ap);
  va_end(ap);

  if ((n < 0) || ((size_t)n >= sizeof(line))) {
    return APP_EIO;
  }

  return diag_router_write_text(line);
}

/** @brief Parses a CLI token into a 32-bit unsigned value. */
static app_status_t cli_parse_u32(const char *s, uint32_t *out)
{
  unsigned long v = 0ul;
  char *end = NULL;

  if ((s == NULL) || (out == NULL) || (*s == '\0')) {
    return APP_EINVAL;
  }

  v = strtoul(s, &end, 0);
  if ((end == NULL) || (*end != '\0') || (v > 0xFFFFFFFFul)) {
    return APP_EINVAL;
  }

  *out = (uint32_t)v;
  return APP_OK;
}

/** @brief Prints decoded ANA event names for an ANA_ANEVENTS register snapshot. */
static void cli_irq_print_ana_events(uint32_t ana_anevents)
{
  if (ana_anevents == 0u) {
    (void)cli_writef("irq ana events=none\r\n");
    return;
  }

  (void)cli_writef("irq ana events=0x%08lX\r\n", (unsigned long)ana_anevents);
  if ((ana_anevents & LAN9645_BIT(24)) != 0u) {
    (void)cli_writef("  - AUTOAGE\r\n");
  }
  if ((ana_anevents & LAN9645_BIT(23)) != 0u) {
    (void)cli_writef("  - LEARN_DROP\r\n");
  }
  if ((ana_anevents & LAN9645_BIT(22)) != 0u) {
    (void)cli_writef("  - AGED_ENTRY\r\n");
  }
  if ((ana_anevents & LAN9645_BIT(21)) != 0u) {
    (void)cli_writef("  - CPU_LEARN_FAILED\r\n");
  }
  if ((ana_anevents & LAN9645_BIT(20)) != 0u) {
    (void)cli_writef("  - AUTO_LEARN_FAILED\r\n");
  }
  if ((ana_anevents & LAN9645_BIT(16)) != 0u) {
    (void)cli_writef("  - DROPPED\r\n");
  }
  if ((ana_anevents & LAN9645_BIT(13)) != 0u) {
    (void)cli_writef("  - VLAN_DISCARD\r\n");
  }
  if ((ana_anevents & LAN9645_BIT(12)) != 0u) {
    (void)cli_writef("  - FWD_DISCARD\r\n");
  }
}

/** @brief Converts a diagnostics event ID into a short CLI label. */
static const char *cli_diag_evt_to_str(diag_event_id_t id)
{
  switch (id) {
    case DIAG_EVT_BOOT: return "BOOT";
    case DIAG_EVT_LAN_SPI_IFSTAT: return "LAN_SPI_IFSTAT";
    case DIAG_EVT_LAN_SPI_TIMEOUT: return "LAN_SPI_TIMEOUT";
    case DIAG_EVT_LAN_PORT_CFG_FAIL: return "LAN_PORT_CFG_FAIL";
    case DIAG_EVT_LAN_VLAN_CFG_FAIL: return "LAN_VLAN_CFG_FAIL";
    case DIAG_EVT_LAN_STRAP_EXPECTATION: return "LAN_STRAP_EXPECTATION";
    case DIAG_EVT_LAN_RMII_LINK_TIMEOUT: return "LAN_RMII_LINK_TIMEOUT";
    case DIAG_EVT_LAN_SERDES0_LINK_TIMEOUT: return "LAN_SERDES0_LINK_TIMEOUT";
    case DIAG_EVT_LAN_SERDES1_LINK_TIMEOUT: return "LAN_SERDES1_LINK_TIMEOUT";
    case DIAG_EVT_LAN_IRQ_STUCK: return "LAN_IRQ_STUCK";
    case DIAG_EVT_LAN_IRQ_CAUSE: return "LAN_IRQ_CAUSE";
    case DIAG_EVT_LAN_IRQ_ACK_FAIL: return "LAN_IRQ_ACK_FAIL";
    case DIAG_EVT_LAN_DROP_COUNTER_ALERT: return "LAN_DROP_COUNTER_ALERT";
    default: return "UNKNOWN";
  }
}

/** @brief Converts a diagnostics owner into a printable string. */
static const char *cli_owner_to_str(app_owner_t owner)
{
  switch (owner) {
    case APP_OWNER_USB: return "usb";
    case APP_OWNER_UART4: return "uart4";
    case APP_OWNER_NONE:
    default:
      return "none";
  }
}

/** @brief Converts HAL Ethernet speed constants into readable text. */
static const char *cli_eth_speed_to_str(uint32_t speed)
{
  if (speed == ETH_SPEED_100M) {
    return "100M";
  }
  if (speed == ETH_SPEED_10M) {
    return "10M";
  }
  return "unknown";
}

/** @brief Converts HAL Ethernet duplex constants into readable text. */
static const char *cli_eth_duplex_to_str(uint32_t duplex)
{
  if (duplex == ETH_FULLDUPLEX_MODE) {
    return "full";
  }
  if (duplex == ETH_HALFDUPLEX_MODE) {
    return "half";
  }
  return "unknown";
}

/** @brief Converts configured LAN port speed into readable text. */
static const char *cli_port_speed_to_str(uint16_t speed_mbps)
{
  if (speed_mbps == LAN9645_CORE_PORT_SPEED_10_MBPS) {
    return "10";
  }
  if (speed_mbps == LAN9645_CORE_PORT_SPEED_100_MBPS) {
    return "100";
  }
  return "unknown";
}

/** @brief Returns 1 when the port belongs to the managed baseline scope (0, 1, 8). */
static uint8_t cli_is_managed_port(uint32_t port)
{
  if ((port == CLI_LAN_CHECK_PORT0) || (port == CLI_LAN_CHECK_PORT1) || (port == CLI_LAN_CHECK_CPU_PORT)) {
    return 1u;
  }
  return 0u;
}

/** @brief Returns 1 when an IPv4 string is the all-zero address. */
static uint8_t cli_ipv4_is_zero(const char *ip)
{
  if (ip == NULL) {
    return 1u;
  }
  return (strcmp(ip, "0.0.0.0") == 0) ? 1u : 0u;
}

/** @brief Lazily prepares the USER_PHY adapter needed by PHY-related CLI commands. */
static app_status_t cli_ensure_phy(void)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }

  if ((s_cli_phy_ready != 0u) && (s_cli_phy.core == core)) {
    return APP_OK;
  }

  st = lan9645_user_phy_adapter_init(&s_cli_phy, core);
  if (st != APP_OK) {
    s_cli_phy_ready = 0u;
    return st;
  }

  st = lan9645_user_phy_adapter_set_active_port(&s_cli_phy, s_cli.active_port);
  if (st != APP_OK) {
    s_cli_phy_ready = 0u;
    return st;
  }

  s_cli_phy_ready = 1u;
  return APP_OK;
}

/** @brief Prints the supported CLI command list. */
static app_status_t cli_cmd_help(void)
{
  (void)cli_writef("cli commands:\r\n");
  (void)cli_writef("  help\r\n");
  (void)cli_writef("  state show\r\n");
  (void)cli_writef("  status all\r\n");
  (void)cli_writef("  status errors\r\n");
  (void)cli_writef("  bringup check\r\n");
  (void)cli_writef("  diag clear\r\n");
  (void)cli_writef("  link show [0|1]\r\n");
  (void)cli_writef("  phy show [0|1]\r\n");
  (void)cli_writef("  lan id\r\n");
  (void)cli_writef("  lan check\r\n");
  (void)cli_writef("  lan profile apply\r\n");
  (void)cli_writef("  lan preflight\r\n");
  (void)cli_writef("  port show [0|1|8]\r\n");
  (void)cli_writef("  port select <0|1>\r\n");
  (void)cli_writef("  port admin <0|1|8> <up|down>\r\n");
  (void)cli_writef("  port mode <0|1|8> <10|100> <half|full>\r\n");
  (void)cli_writef("  vlan set <vid> <member_mask_hex> <untag_mask_hex>\r\n");
  (void)cli_writef("  vlan show <vid>\r\n");
  (void)cli_writef("  eth show\r\n");
  (void)cli_writef("  net show\r\n");
  (void)cli_writef("  net check\r\n");
  (void)cli_writef("  serial show\r\n");
  (void)cli_writef("  irq show\r\n");
  (void)cli_writef("  irq clear\r\n");
  (void)cli_writef("  irq ack\r\n");
  (void)cli_writef("  spi status\r\n");
  (void)cli_writef("  spi stats\r\n");
  (void)cli_writef("  spi stats clear\r\n");
  (void)cli_writef("  reg read <0xADDR>\r\n");
  (void)cli_writef("  reg write <0xADDR> <0xVALUE>\r\n");
  (void)cli_writef("  sanity spi <0xADDR> [count]\r\n");
  return APP_OK;
}

/** @brief Prints low-level Ethernet runtime state. */
static app_status_t cli_cmd_eth_show(void)
{
  ethernetif_runtime_state_t eth = {0};
  app_status_t st = ethernetif_get_runtime_state(&eth);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "eth link=%u started=%u user_phy=%u phy_state=%ld speed=%s duplex=%s t=%lums\r\n",
    (unsigned int)eth.link_up,
    (unsigned int)eth.eth_started,
    (unsigned int)eth.user_phy_ready,
    (long)eth.last_phy_status,
    cli_eth_speed_to_str(eth.mac_speed),
    cli_eth_duplex_to_str(eth.mac_duplex),
    (unsigned long)eth.sample_tick_ms
  );
  return APP_OK;
}

/** @brief Prints LwIP runtime state and IPv4 configuration. */
static app_status_t cli_cmd_net_show(void)
{
  lwip_runtime_state_t net = {0};
  app_status_t st = lwip_get_runtime_state(&net);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "net up=%u link=%u default=%u mtu=%u flags=0x%04X t=%lums\r\n",
    (unsigned int)net.netif_up,
    (unsigned int)net.link_up,
    (unsigned int)net.is_default,
    (unsigned int)net.mtu,
    (unsigned int)net.flags,
    (unsigned long)net.sample_tick_ms
  );
  (void)cli_writef(
    "net ip=%s mask=%s gw=%s mac=%02X:%02X:%02X:%02X:%02X:%02X\r\n",
    net.ip,
    net.netmask,
    net.gateway,
    (unsigned int)net.mac[0],
    (unsigned int)net.mac[1],
    (unsigned int)net.mac[2],
    (unsigned int)net.mac[3],
    (unsigned int)net.mac[4],
    (unsigned int)net.mac[5]
  );

  return APP_OK;
}

/** @brief Verifies that the network stack is ready for ping testing. */
static app_status_t cli_cmd_net_check(void)
{
  lwip_runtime_state_t net = {0};
  ethernetif_runtime_state_t eth = {0};
  uint8_t pass = 1u;
  app_status_t st = lwip_get_runtime_state(&net);
  if (st != APP_OK) {
    return st;
  }
  st = ethernetif_get_runtime_state(&eth);
  if (st != APP_OK) {
    return st;
  }

  if (net.netif_up == 0u) {
    pass = 0u;
  }
  (void)cli_writef("net check netif_up=%u %s\r\n", (unsigned int)net.netif_up, (net.netif_up != 0u) ? "OK" : "FAIL");

  if (net.link_up == 0u) {
    pass = 0u;
  }
  (void)cli_writef("net check link_up=%u %s\r\n", (unsigned int)net.link_up, (net.link_up != 0u) ? "OK" : "FAIL");

  if (net.is_default == 0u) {
    pass = 0u;
  }
  (void)cli_writef("net check default=%u %s\r\n", (unsigned int)net.is_default, (net.is_default != 0u) ? "OK" : "FAIL");

  if (cli_ipv4_is_zero(net.ip) != 0u) {
    pass = 0u;
  }
  (void)cli_writef("net check ip=%s %s\r\n", net.ip, (cli_ipv4_is_zero(net.ip) == 0u) ? "OK" : "FAIL");

  if (cli_ipv4_is_zero(net.netmask) != 0u) {
    pass = 0u;
  }
  (void)cli_writef("net check mask=%s %s\r\n", net.netmask, (cli_ipv4_is_zero(net.netmask) == 0u) ? "OK" : "FAIL");

  if (eth.user_phy_ready == 0u) {
    pass = 0u;
  }
  (void)cli_writef("net check user_phy_ready=%u %s\r\n", (unsigned int)eth.user_phy_ready, (eth.user_phy_ready != 0u) ? "OK" : "FAIL");

  if (eth.eth_started == 0u) {
    pass = 0u;
  }
  (void)cli_writef("net check eth_started=%u %s\r\n", (unsigned int)eth.eth_started, (eth.eth_started != 0u) ? "OK" : "FAIL");

  (void)cli_writef("net check result=%s\r\n", (pass != 0u) ? "PASS" : "FAIL");
  return (pass != 0u) ? APP_OK : APP_EIO;
}

/** @brief Prints serial routing and CLI ingress statistics. */
static app_status_t cli_cmd_serial_show(void)
{
  diag_router_state_t router = {0};
  cli_ingress_stats_t ingress = {0};
  app_status_t st = APP_OK;

  st = diag_router_get_state(&router);
  if (st != APP_OK) {
    return st;
  }
  st = cli_ingress_get_stats(&ingress);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "serial owner=%s usb_ready=%u uart_ready=%u mirror=%u bridge=%u\r\n",
    cli_owner_to_str(router.owner),
    (unsigned int)router.usb_ready,
    (unsigned int)router.uart_ready,
    (unsigned int)router.mirror_when_none,
    (unsigned int)router.bridge_usb_uart_on
  );

  (void)cli_writef(
    "serial ingress bytes_usb=%lu bytes_uart4=%lu lines_q=%lu lines_exec=%lu line_ovf=%lu queue_ovf=%lu cmd_fail=%lu queue=%u/%u\r\n",
    (unsigned long)ingress.bytes_usb,
    (unsigned long)ingress.bytes_uart4,
    (unsigned long)ingress.lines_queued,
    (unsigned long)ingress.lines_executed,
    (unsigned long)ingress.line_overflow_count,
    (unsigned long)ingress.queue_overflow_count,
    (unsigned long)ingress.command_fail_count,
    (unsigned int)ingress.queue_count,
    (unsigned int)ingress.queue_depth
  );

  return APP_OK;
}

/** @brief Prints the current application state-machine snapshot. */
static app_status_t cli_cmd_state_show(void)
{
  app_orchestrator_snapshot_t snap = {0};
  app_status_t st = app_orchestrator_get_snapshot(&snap);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "state=%s prev=%s t=%lums fault=%lu\r\n",
    app_orchestrator_state_name(snap.state),
    app_orchestrator_state_name(snap.prev_state),
    (unsigned long)snap.last_transition_ms,
    (unsigned long)snap.fault_code
  );
  return APP_OK;
}

/** @brief Clears all stored diagnostics counters. */
static app_status_t cli_cmd_diag_clear(void)
{
  app_status_t st = diag_core_clear_all();
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef("diag cleared\r\n");
  return APP_OK;
}

/** @brief Prints non-zero diagnostics counters. */
static app_status_t cli_cmd_status_errors(void)
{
  uint8_t found = 0u;
  uint32_t i = 0u;

  for (i = 0u; i < (uint32_t)DIAG_EVT_COUNT; ++i) {
    diag_error_counter_t ctr = {0};
    if (diag_core_get_error((diag_event_id_t)i, &ctr) != APP_OK) {
      continue;
    }
    if (ctr.count > 0u) {
      found = 1u;
      (void)cli_writef(
        "%s count=%lu last_dev=%lu last_reg=0x%08lX last_arg0=%lu last_arg1=%lu\r\n",
        cli_diag_evt_to_str((diag_event_id_t)i),
        (unsigned long)ctr.count,
        (unsigned long)ctr.last_dev,
        (unsigned long)ctr.last_reg,
        (unsigned long)ctr.last_arg0,
        (unsigned long)ctr.last_arg1
      );
    }
  }

  if (found == 0u) {
    (void)cli_writef("no errors recorded\r\n");
  }

  return APP_OK;
}

/** @brief Prints the interpreted link state for one copper port. */
static app_status_t cli_cmd_link_show(uint8_t port)
{
  int32_t link_state = 0;
  app_status_t st = cli_ensure_phy();
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_user_phy_adapter_set_active_port(&s_cli_phy, port);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_user_phy_adapter_get_link_state(&s_cli_phy, port, &link_state);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef("link port%u state=%ld\r\n", (unsigned int)port, (long)link_state);
  return APP_OK;
}

/** @brief Dumps the synthetic PHY register view for one copper port. */
static app_status_t cli_cmd_phy_show(uint8_t port)
{
  uint32_t id1 = 0u;
  uint32_t id2 = 0u;
  uint32_t bcr = 0u;
  uint32_t bsr = 0u;
  uint32_t anar = 0u;
  uint32_t anlpar = 0u;
  app_status_t st = cli_ensure_phy();
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_user_phy_adapter_set_active_port(&s_cli_phy, port);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_user_phy_adapter_read_reg(&s_cli_phy, port, LAN9645_USER_PHY_REG_PHYI1R, &id1);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_user_phy_adapter_read_reg(&s_cli_phy, port, LAN9645_USER_PHY_REG_PHYI2R, &id2);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_user_phy_adapter_read_reg(&s_cli_phy, port, LAN9645_USER_PHY_REG_BCR, &bcr);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_user_phy_adapter_read_reg(&s_cli_phy, port, LAN9645_USER_PHY_REG_BSR, &bsr);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_user_phy_adapter_read_reg(&s_cli_phy, port, LAN9645_USER_PHY_REG_ANAR, &anar);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_user_phy_adapter_read_reg(&s_cli_phy, port, LAN9645_USER_PHY_REG_ANLPAR, &anlpar);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "phy port%u id1=0x%04lX id2=0x%04lX bcr=0x%04lX bsr=0x%04lX anar=0x%04lX anlpar=0x%04lX\r\n",
    (unsigned int)port,
    (unsigned long)id1,
    (unsigned long)id2,
    (unsigned long)bcr,
    (unsigned long)bsr,
    (unsigned long)anar,
    (unsigned long)anlpar
  );
  return APP_OK;
}

/** @brief Prints administrative and MAC mode state for one managed switch port. */
static app_status_t cli_cmd_port_show_cfg(uint8_t port)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_core_port_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if (cli_is_managed_port((uint32_t)port) == 0u) {
    return APP_EINVAL;
  }

  st = lan9645_core_get_port_cfg(core, port, &cfg);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "port cfg port=%u admin=%s speed=%s duplex=%s mac_rx=%u mac_tx=%u\r\n",
    (unsigned int)port,
    (cfg.admin_up != 0u) ? "up" : "down",
    cli_port_speed_to_str(cfg.speed_mbps),
    (cfg.full_duplex != 0u) ? "full" : "half",
    (unsigned int)cfg.mac_rx_ena,
    (unsigned int)cfg.mac_tx_ena
  );
  return APP_OK;
}

/** @brief Applies one administrative up/down command on a managed switch port. */
static app_status_t cli_cmd_port_admin(uint8_t port, uint8_t up)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if (cli_is_managed_port((uint32_t)port) == 0u) {
    return APP_EINVAL;
  }

  st = lan9645_core_set_port_admin(core, port, up);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "port admin port=%u state=%s\r\n",
    (unsigned int)port,
    (up != 0u) ? "up" : "down"
  );
  return cli_cmd_port_show_cfg(port);
}

/** @brief Applies one 10/100 MAC mode command on a managed switch port. */
static app_status_t cli_cmd_port_mode(uint8_t port, uint16_t speed_mbps, uint8_t full_duplex)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if (cli_is_managed_port((uint32_t)port) == 0u) {
    return APP_EINVAL;
  }

  st = lan9645_core_set_port_mode(core, port, speed_mbps, full_duplex);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "port mode port=%u speed=%s duplex=%s\r\n",
    (unsigned int)port,
    cli_port_speed_to_str(speed_mbps),
    (full_duplex != 0u) ? "full" : "half"
  );
  return cli_cmd_port_show_cfg(port);
}

/** @brief Applies one VLAN baseline entry using PGID-based managed profile control. */
static app_status_t cli_cmd_vlan_set(uint16_t vid, uint16_t member_mask, uint16_t untag_mask)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }

  st = lan9645_vlan_set(core, vid, member_mask, untag_mask);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "vlan set vid=%u member=0x%03X untag=0x%03X\r\n",
    (unsigned int)vid,
    (unsigned int)member_mask,
    (unsigned int)untag_mask
  );
  return APP_OK;
}

/** @brief Prints one VLAN baseline entry snapshot. */
static app_status_t cli_cmd_vlan_show(uint16_t vid)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_vlan_entry_t entry = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }

  st = lan9645_vlan_get(core, vid, &entry);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "vlan vid=%u configured=%u obey=%u member=0x%03X untag=0x%03X\r\n",
    (unsigned int)vid,
    (unsigned int)entry.configured,
    (unsigned int)entry.obey_vlan,
    (unsigned int)entry.member_mask,
    (unsigned int)entry.untag_mask
  );
  return APP_OK;
}

/** @brief Prints a combined application, LAN, IRQ, ETH, and diagnostics snapshot. */
static app_status_t cli_cmd_status_all(void)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  diag_event_t last = {0};
  app_orchestrator_snapshot_t snap = {0};
  lan9645_irq_state_t irq_state = {0};

  (void)cli_writef("status active_port=%u\r\n", (unsigned int)s_cli.active_port);
  if (app_orchestrator_get_snapshot(&snap) == APP_OK) {
    (void)cli_writef(
      "status app_state=%s prev=%s fault=%lu\r\n",
      app_orchestrator_state_name(snap.state),
      app_orchestrator_state_name(snap.prev_state),
      (unsigned long)snap.fault_code
    );
  }
  (void)cli_writef(
    "status cli cmd_count=%lu cmd_err=%lu\r\n",
    (unsigned long)s_cli.command_count,
    (unsigned long)s_cli.command_error_count
  );

  if ((core != NULL) && (core->initialized != 0u)) {
    uint32_t feat = 0u;
    (void)cli_cmd_spi_status();
    (void)cli_cmd_spi_stats();
    if (lan9645_core_read_feat_disable(core, &feat) == APP_OK) {
      (void)cli_writef("lan feat_disable=0x%08lX\r\n", (unsigned long)feat);
    } else {
      (void)cli_writef("lan feat_disable=unavailable\r\n");
    }
  } else {
    (void)cli_writef("lan core not ready\r\n");
  }

  if (lan9645_runtime_irq_get(&irq_state) == APP_OK) {
    (void)cli_writef(
      "irq line=%u edges=%lu stuck=%u timeout=%lu last_edge_ms=%lu\r\n",
      (unsigned int)irq_state.line_high,
      (unsigned long)irq_state.edge_count,
      (unsigned int)irq_state.stuck_latched,
      (unsigned long)irq_state.stuck_timeout_ms,
      (unsigned long)irq_state.last_edge_ms
    );
  }

  (void)cli_cmd_eth_show();
  (void)cli_cmd_net_show();

  if (diag_core_get_last(&last) == APP_OK) {
    (void)cli_writef(
      "last event=%s dev=%lu reg=0x%08lX arg0=%lu arg1=%lu\r\n",
      cli_diag_evt_to_str(last.id),
      (unsigned long)last.dev,
      (unsigned long)last.reg,
      (unsigned long)last.arg0,
      (unsigned long)last.arg1
    );
  } else {
    (void)cli_writef("last event=none\r\n");
  }

  (void)cli_cmd_link_show(s_cli.active_port);
  return cli_cmd_status_errors();
}

/** @brief Prints IRQ-line state and the relevant LAN9645 interrupt registers. */
static app_status_t cli_cmd_irq_show(void)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_irq_state_t irq_state = {0};
  lan9645_core_irq_regs_t irq_regs = {0};
  app_status_t st = lan9645_runtime_irq_get(&irq_state);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "irq line=%u edges=%lu stuck=%u timeout=%lu last_edge_ms=%lu high_since_ms=%lu\r\n",
    (unsigned int)irq_state.line_high,
    (unsigned long)irq_state.edge_count,
    (unsigned int)irq_state.stuck_latched,
    (unsigned long)irq_state.stuck_timeout_ms,
    (unsigned long)irq_state.last_edge_ms,
    (unsigned long)irq_state.high_since_ms
  );

  if ((core != NULL) && (core->initialized != 0u) && (core->reg != NULL)) {
    st = lan9645_core_get_irq_regs(core, &irq_regs);
    if (st == APP_OK) {
      (void)cli_writef(
        "irq regs ana_anaintr=0x%08lX ana_anevents=0x%08lX intr_ena=0x%08lX intr_ident=0x%08lX intr_ident1=0x%08lX dev_intr_ena=0x%08lX dev_intr_ident=0x%08lX\r\n",
        (unsigned long)irq_regs.ana_anaintr,
        (unsigned long)irq_regs.ana_anevents,
        (unsigned long)irq_regs.intr_ena,
        (unsigned long)irq_regs.intr_ident,
        (unsigned long)irq_regs.intr_ident1,
        (unsigned long)irq_regs.dev_intr_ena,
        (unsigned long)irq_regs.dev_intr_ident
      );
      (void)cli_writef(
        "irq ana intr=%lu intr_ena=%lu dev_mask pending=0x%03lX enabled=0x%03lX\r\n",
        (unsigned long)LAN9645_FIELD_GET(LAN9645_ANA_ANAINTR_INTR, irq_regs.ana_anaintr),
        (unsigned long)LAN9645_FIELD_GET(LAN9645_ANA_ANAINTR_INTR_ENA, irq_regs.ana_anaintr),
        (unsigned long)LAN9645_FIELD_GET(LAN9645_CPU_DEV_INTR_MASK, irq_regs.dev_intr_ident),
        (unsigned long)LAN9645_FIELD_GET(LAN9645_CPU_DEV_INTR_MASK, irq_regs.dev_intr_ena)
      );
      cli_irq_print_ana_events(irq_regs.ana_anevents);
    } else {
      (void)cli_writef("irq regs unavailable st=%ld\r\n", (long)st);
    }
  }

  return APP_OK;
}

/** @brief Clears IRQ runtime counters. */
static app_status_t cli_cmd_irq_clear(void)
{
  app_status_t st = lan9645_runtime_irq_clear();
  if (st != APP_OK) {
    return st;
  }
  (void)cli_writef("irq counters cleared\r\n");
  return APP_OK;
}

/** @brief Acknowledges currently latched ANA interrupt events. */
static app_status_t cli_cmd_irq_ack(void)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_core_irq_regs_t irq_regs = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }

  st = lan9645_core_get_irq_regs(core, &irq_regs);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_core_ack_irq(core, irq_regs.ana_anevents);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef("irq ack ana_anevents=0x%08lX\r\n", (unsigned long)irq_regs.ana_anevents);
  return APP_OK;
}

/** @brief Reads one raw LAN9645 register by absolute address. */
static app_status_t cli_cmd_reg_read(uint32_t addr)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  uint32_t val = 0u;
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }

  st = lan9645_reg_read32(core->reg, addr, &val);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef("reg[0x%08lX]=0x%08lX\r\n", (unsigned long)addr, (unsigned long)val);
  return APP_OK;
}

/** @brief Writes and reads back one raw LAN9645 register by absolute address. */
static app_status_t cli_cmd_reg_write(uint32_t addr, uint32_t value)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  uint32_t verify = 0u;
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }

  st = lan9645_reg_write32(core->reg, addr, value);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_reg_read32(core->reg, addr, &verify);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "reg[0x%08lX]<=0x%08lX verify=0x%08lX\r\n",
    (unsigned long)addr,
    (unsigned long)value,
    (unsigned long)verify
  );
  return APP_OK;
}

/** @brief Repeatedly reads one SPI register to expose unstable traffic. */
static app_status_t cli_cmd_sanity_spi(uint32_t addr, uint32_t count)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  uint32_t i = 0u;
  uint32_t first = 0u;
  uint32_t value = 0u;
  uint32_t read_fail = 0u;
  uint32_t mismatch = 0u;
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if (count == 0u) {
    return APP_EINVAL;
  }
  if (count > 10000u) {
    return APP_EINVAL;
  }

  st = lan9645_reg_read32(core->reg, addr, &first);
  if (st != APP_OK) {
    return st;
  }

  for (i = 0u; i < count; ++i) {
    st = lan9645_reg_read32(core->reg, addr, &value);
    if (st != APP_OK) {
      read_fail++;
      continue;
    }
    if (value != first) {
      mismatch++;
    }
  }

  (void)cli_writef(
    "sanity spi addr=0x%08lX first=0x%08lX loops=%lu read_fail=%lu mismatch=%lu\r\n",
    (unsigned long)addr,
    (unsigned long)first,
    (unsigned long)count,
    (unsigned long)read_fail,
    (unsigned long)mismatch
  );
  return APP_OK;
}

/** @brief Prints the current LAN9645 SPI management interface status. */
static app_status_t cli_cmd_spi_status(void)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  uint32_t if_ctrl = 0u;
  uint32_t if_cfgstat = 0u;
  uint32_t spi_hz = 0u;
  uint8_t padding = 0u;
  uint8_t ready = 0u;
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }

  st = lan9645_core_get_spi_status(core, NULL, NULL, &padding, &spi_hz, &ready);
  if (st != APP_OK) {
    return st;
  }

  if (ready == 0u) {
    (void)cli_writef(
      "spi status ready=0 spi_hz=%lu padding=%u\r\n",
      (unsigned long)spi_hz,
      (unsigned int)padding
    );
    return APP_OK;
  }

  st = lan9645_core_get_spi_status(core, &if_ctrl, &if_cfgstat, &padding, &spi_hz, &ready);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "spi status ready=%u spi_hz=%lu padding=%u if_ctrl=0x%08lX if_cfgstat=0x%08lX if_cfg=%lu if_stat=%lu\r\n",
    (unsigned int)ready,
    (unsigned long)spi_hz,
    (unsigned int)padding,
    (unsigned long)if_ctrl,
    (unsigned long)if_cfgstat,
    (unsigned long)LAN9645_FIELD_GET(LAN9645_GCB_IF_CFGSTAT_IF_CFG, if_cfgstat),
    (unsigned long)LAN9645_FIELD_GET(LAN9645_GCB_IF_CFGSTAT_IF_STAT, if_cfgstat)
  );

  return APP_OK;
}

/** @brief Prints SPI transport telemetry counters from the LAN9645 bus layer. */
static app_status_t cli_cmd_spi_stats(void)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_spi_stats_t stats = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL) || (core->reg->spi == NULL)) {
    return APP_ESTATE;
  }

  st = lan9645_spi_get_stats(core->reg->spi, &stats);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "spi stats rd=%lu wr=%lu retry=%lu fail=%lu timeout=%lu inval=%lu last_st=%ld last_addr=0x%08lX\r\n",
    (unsigned long)stats.read_count,
    (unsigned long)stats.write_count,
    (unsigned long)stats.retry_count,
    (unsigned long)stats.failure_count,
    (unsigned long)stats.timeout_count,
    (unsigned long)stats.invalid_arg_count,
    (long)stats.last_status,
    (unsigned long)stats.last_reg_addr
  );

  return APP_OK;
}

/** @brief Resets SPI transport telemetry counters. */
static app_status_t cli_cmd_spi_stats_clear(void)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL) || (core->reg->spi == NULL)) {
    return APP_ESTATE;
  }

  st = lan9645_spi_reset_stats(core->reg->spi);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef("spi stats cleared\r\n");
  return APP_OK;
}

/** @brief Reads and decodes the LAN9645 chip identifier. */
static app_status_t cli_cmd_lan_id(void)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  uint32_t chip_id = 0u;
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }

  st = lan9645_core_read_chip_id(core, &chip_id);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "lan id chip=0x%08lX rev=%lu part=0x%04lX mfg=0x%03lX one=%lu\r\n",
    (unsigned long)chip_id,
    (unsigned long)LAN9645_FIELD_GET(LAN9645_GCB_CHIP_ID_REV_ID, chip_id),
    (unsigned long)LAN9645_FIELD_GET(LAN9645_GCB_CHIP_ID_PART_ID, chip_id),
    (unsigned long)LAN9645_FIELD_GET(LAN9645_GCB_CHIP_ID_MFG_ID, chip_id),
    (unsigned long)LAN9645_FIELD_GET(LAN9645_GCB_CHIP_ID_ONE, chip_id)
  );

  return APP_OK;
}

/** @brief Checks that one port matches the fixed managed profile. */
static app_status_t cli_cmd_lan_check_port(lan9645_core_t *core, uint32_t port)
{
  uint32_t v = 0u;
  uint32_t speed = 0u;
  uint8_t ok = 1u;
  app_status_t st = APP_OK;

  st = lan9645_reg_read32_desc(core->reg, &LAN9645_REG_QSYS_SW_PORT_MODE, 0u, 0u, port, &v);
  if (st != APP_OK) {
    return st;
  }
  if ((v & LAN9645_QSYS_SW_PORT_MODE_PORT_ENA) == 0u) {
    ok = 0u;
  }

  st = lan9645_reg_read32_desc(core->reg, &LAN9645_REG_DEV_CLOCK_CFG, 0u, port, 0u, &v);
  if (st != APP_OK) {
    return st;
  }
  speed = LAN9645_FIELD_GET(LAN9645_DEV_CLOCK_CFG_LINK_SPEED, v);
  if ((speed != CLI_LAN_CHECK_SPEED100) ||
      ((v & (LAN9645_DEV_CLOCK_CFG_MAC_TX_RST |
             LAN9645_DEV_CLOCK_CFG_MAC_RX_RST |
             LAN9645_DEV_CLOCK_CFG_PCS_TX_RST |
             LAN9645_DEV_CLOCK_CFG_PCS_RX_RST |
             LAN9645_DEV_CLOCK_CFG_PORT_RST |
             LAN9645_DEV_CLOCK_CFG_PHY_RST)) != 0u)) {
    ok = 0u;
  }

  st = lan9645_reg_read32_desc(core->reg, &LAN9645_REG_DEV_MAC_MODE_CFG, 0u, port, 0u, &v);
  if (st != APP_OK) {
    return st;
  }
  if (((v & LAN9645_DEV_MAC_MODE_CFG_FDX_ENA) == 0u) ||
      ((v & LAN9645_DEV_MAC_MODE_CFG_GIGA_MODE_ENA) != 0u)) {
    ok = 0u;
  }

  st = lan9645_reg_read32_desc(core->reg, &LAN9645_REG_DEV_MAC_ENA_CFG, 0u, port, 0u, &v);
  if (st != APP_OK) {
    return st;
  }
  if ((v & (LAN9645_DEV_MAC_ENA_CFG_TX_ENA | LAN9645_DEV_MAC_ENA_CFG_RX_ENA)) !=
      (LAN9645_DEV_MAC_ENA_CFG_TX_ENA | LAN9645_DEV_MAC_ENA_CFG_RX_ENA)) {
    ok = 0u;
  }

  (void)cli_writef(
    "lan check port%lu %s\r\n",
    (unsigned long)port,
    (ok != 0u) ? "OK" : "FAIL"
  );

  return (ok != 0u) ? APP_OK : APP_EIO;
}

/** @brief Runs the LAN9645 configuration sanity checks used before ping tests. */
static app_status_t cli_cmd_lan_check(void)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  uint32_t chip_id = 0u;
  uint32_t ext_cpu_cfg = 0u;
  uint32_t ana_anaintr = 0u;
  uint32_t cpu_dev_intr_ena = 0u;
  uint8_t pass = 1u;
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }

  st = cli_cmd_spi_status();
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_core_read_chip_id(core, &chip_id);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef("lan check chip_id=0x%08lX\r\n", (unsigned long)chip_id);

  st = lan9645_reg_read32_desc(core->reg, &LAN9645_REG_QSYS_EXT_CPU_CFG, 0u, 0u, 0u, &ext_cpu_cfg);
  if (st != APP_OK) {
    return st;
  }

  if ((LAN9645_FIELD_GET(LAN9645_QSYS_EXT_CPU_CFG_EXT_CPU_PORT, ext_cpu_cfg) != CLI_LAN_CHECK_CPU_PORT) ||
      (LAN9645_FIELD_GET(LAN9645_QSYS_EXT_CPU_CFG_EXT_CPUQ_MSK, ext_cpu_cfg) != 0xFFu) ||
      ((ext_cpu_cfg & LAN9645_QSYS_EXT_CPU_CFG_EXT_CPU_KILL_ENA) == 0u) ||
      ((ext_cpu_cfg & LAN9645_QSYS_EXT_CPU_CFG_INT_CPU_KILL_ENA) == 0u)) {
    pass = 0u;
    (void)cli_writef("lan check ext_cpu_cfg FAIL val=0x%08lX\r\n", (unsigned long)ext_cpu_cfg);
  } else {
    (void)cli_writef("lan check ext_cpu_cfg OK val=0x%08lX\r\n", (unsigned long)ext_cpu_cfg);
  }

  st = lan9645_reg_read32_desc(core->reg, &LAN9645_REG_ANA_ANAINTR, 0u, 0u, 0u, &ana_anaintr);
  if (st != APP_OK) {
    return st;
  }

  if ((ana_anaintr & LAN9645_ANA_ANAINTR_INTR_ENA) == 0u) {
    pass = 0u;
    (void)cli_writef("lan check anaintr FAIL val=0x%08lX\r\n", (unsigned long)ana_anaintr);
  } else {
    (void)cli_writef("lan check anaintr OK val=0x%08lX\r\n", (unsigned long)ana_anaintr);
  }

  st = lan9645_reg_read32_desc(core->reg, &LAN9645_REG_CPU_DEV_INTR_ENA, 0u, 0u, 0u, &cpu_dev_intr_ena);
  if (st != APP_OK) {
    return st;
  }

  if (LAN9645_FIELD_GET(LAN9645_CPU_DEV_INTR_MASK, cpu_dev_intr_ena) != CLI_LAN_CHECK_CPU_DEV_IRQ_MASK) {
    pass = 0u;
    (void)cli_writef("lan check cpu_dev_intr_ena FAIL val=0x%08lX\r\n", (unsigned long)cpu_dev_intr_ena);
  } else {
    (void)cli_writef("lan check cpu_dev_intr_ena OK val=0x%08lX\r\n", (unsigned long)cpu_dev_intr_ena);
  }

  if (cli_cmd_lan_check_port(core, CLI_LAN_CHECK_PORT0) != APP_OK) {
    pass = 0u;
  }
  if (cli_cmd_lan_check_port(core, CLI_LAN_CHECK_PORT1) != APP_OK) {
    pass = 0u;
  }
  if (cli_cmd_lan_check_port(core, CLI_LAN_CHECK_CPU_PORT) != APP_OK) {
    pass = 0u;
  }

  (void)cli_writef("lan check result=%s\r\n", (pass != 0u) ? "PASS" : "FAIL");
  return (pass != 0u) ? APP_OK : APP_EIO;
}

/** @brief Applies the fixed managed-switch profile. */
static app_status_t cli_cmd_lan_profile_apply(void)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_profile_result_t profile = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }

  st = lan9645_profile_managed_rmii1_apply(core, &profile);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "lan profile apply OK chip=0x%08lX feat=0x%08lX ext_cpu_cfg=0x%08lX ports_mask=0x%08lX\r\n",
    (unsigned long)profile.chip_id,
    (unsigned long)profile.feat_disable,
    (unsigned long)profile.ext_cpu_cfg,
    (unsigned long)profile.ports_enabled_mask
  );

  return APP_OK;
}

/** @brief Runs the LAN preflight sequence used before network bring-up. */
static app_status_t cli_cmd_lan_preflight(void)
{
  app_status_t st = APP_OK;
  uint8_t pass = 1u;

  st = cli_cmd_spi_status();
  if (st != APP_OK) {
    pass = 0u;
  }

  st = cli_cmd_lan_id();
  if (st != APP_OK) {
    pass = 0u;
  }

  st = cli_cmd_lan_check();
  if (st != APP_OK) {
    pass = 0u;
  }

  st = cli_cmd_link_show(0u);
  if (st != APP_OK) {
    pass = 0u;
  }

  st = cli_cmd_link_show(1u);
  if (st != APP_OK) {
    pass = 0u;
  }

  (void)cli_writef("lan preflight result=%s\r\n", (pass != 0u) ? "PASS" : "FAIL");
  return (pass != 0u) ? APP_OK : APP_EIO;
}

/** @brief Runs the high-level bring-up checklist used before the ping matrix. */
static app_status_t cli_cmd_bringup_check(void)
{
  uint8_t pass = 1u;

  if (cli_cmd_serial_show() != APP_OK) {
    pass = 0u;
  }
  if (cli_cmd_eth_show() != APP_OK) {
    pass = 0u;
  }
  if (cli_cmd_net_show() != APP_OK) {
    pass = 0u;
  }
  if (cli_cmd_net_check() != APP_OK) {
    pass = 0u;
  }
  if (cli_cmd_lan_preflight() != APP_OK) {
    pass = 0u;
  }

  (void)cli_writef("bringup check result=%s\r\n", (pass != 0u) ? "PASS" : "FAIL");
  return (pass != 0u) ? APP_OK : APP_EIO;
}

/** @brief Dispatches one tokenized CLI command. */
static app_status_t cli_exec_tokens(char *tokens[], uint32_t ntok)
{
  if ((tokens == NULL) || (ntok == 0u)) {
    return APP_EINVAL;
  }

  if (cli_eq(tokens[0], "help") != 0u) {
    return cli_cmd_help();
  }

  if (cli_eq(tokens[0], "status") != 0u) {
    if (ntok < 2u) {
      return APP_EINVAL;
    }
    if (cli_eq(tokens[1], "all") != 0u) {
      return cli_cmd_status_all();
    }
    if (cli_eq(tokens[1], "errors") != 0u) {
      return cli_cmd_status_errors();
    }
    return APP_EINVAL;
  }

  if (cli_eq(tokens[0], "bringup") != 0u) {
    if ((ntok < 2u) || (cli_eq(tokens[1], "check") == 0u)) {
      return APP_EINVAL;
    }
    return cli_cmd_bringup_check();
  }

  if (cli_eq(tokens[0], "state") != 0u) {
    if ((ntok < 2u) || (cli_eq(tokens[1], "show") == 0u)) {
      return APP_EINVAL;
    }
    return cli_cmd_state_show();
  }

  if (cli_eq(tokens[0], "diag") != 0u) {
    if ((ntok < 2u) || (cli_eq(tokens[1], "clear") == 0u)) {
      return APP_EINVAL;
    }
    return cli_cmd_diag_clear();
  }

  if (cli_eq(tokens[0], "link") != 0u) {
    uint8_t port = s_cli.active_port;
    uint32_t p = 0u;
    if ((ntok < 2u) || (cli_eq(tokens[1], "show") == 0u)) {
      return APP_EINVAL;
    }
    if (ntok >= 3u) {
      if (cli_parse_u32(tokens[2], &p) != APP_OK) {
        return APP_EINVAL;
      }
      if (p >= LAN9645_USER_PHY_PORT_COUNT) {
        return APP_EINVAL;
      }
      port = (uint8_t)p;
    }
    return cli_cmd_link_show(port);
  }

  if (cli_eq(tokens[0], "phy") != 0u) {
    uint8_t port = s_cli.active_port;
    uint32_t p = 0u;
    if ((ntok < 2u) || (cli_eq(tokens[1], "show") == 0u)) {
      return APP_EINVAL;
    }
    if (ntok >= 3u) {
      if (cli_parse_u32(tokens[2], &p) != APP_OK) {
        return APP_EINVAL;
      }
      if (p >= LAN9645_USER_PHY_PORT_COUNT) {
        return APP_EINVAL;
      }
      port = (uint8_t)p;
    }
    return cli_cmd_phy_show(port);
  }

  if (cli_eq(tokens[0], "lan") != 0u) {
    if (ntok < 2u) {
      return APP_EINVAL;
    }
    if (cli_eq(tokens[1], "id") != 0u) {
      return cli_cmd_lan_id();
    }
    if (cli_eq(tokens[1], "check") != 0u) {
      return cli_cmd_lan_check();
    }
    if (cli_eq(tokens[1], "profile") != 0u) {
      if ((ntok >= 3u) && (cli_eq(tokens[2], "apply") != 0u)) {
        return cli_cmd_lan_profile_apply();
      }
      return APP_EINVAL;
    }
    if (cli_eq(tokens[1], "preflight") != 0u) {
      return cli_cmd_lan_preflight();
    }
    return APP_EINVAL;
  }

  if (cli_eq(tokens[0], "port") != 0u) {
    if (ntok < 2u) {
      return APP_EINVAL;
    }
    if (cli_eq(tokens[1], "show") != 0u) {
      uint8_t port = s_cli.active_port;
      uint32_t p = 0u;
      if (ntok >= 3u) {
        if (cli_parse_u32(tokens[2], &p) != APP_OK) {
          return APP_EINVAL;
        }
        if (cli_is_managed_port(p) == 0u) {
          return APP_EINVAL;
        }
        port = (uint8_t)p;
      }
      (void)cli_writef("active port=%u\r\n", (unsigned int)s_cli.active_port);
      return cli_cmd_port_show_cfg(port);
    }
    if (cli_eq(tokens[1], "select") != 0u) {
      uint32_t p = 0u;
      if (ntok < 3u) {
        return APP_EINVAL;
      }
      if (cli_parse_u32(tokens[2], &p) != APP_OK) {
        return APP_EINVAL;
      }
      if (p >= LAN9645_USER_PHY_PORT_COUNT) {
        return APP_EINVAL;
      }
      s_cli.active_port = (uint8_t)p;
      if (s_cli_phy_ready != 0u) {
        (void)lan9645_user_phy_adapter_set_active_port(&s_cli_phy, s_cli.active_port);
      }
      (void)cli_writef("active port set=%u\r\n", (unsigned int)s_cli.active_port);
      return APP_OK;
    }
    if (cli_eq(tokens[1], "admin") != 0u) {
      uint32_t p = 0u;
      uint8_t up = 0u;
      if (ntok < 4u) {
        return APP_EINVAL;
      }
      if (cli_parse_u32(tokens[2], &p) != APP_OK) {
        return APP_EINVAL;
      }
      if (cli_is_managed_port(p) == 0u) {
        return APP_EINVAL;
      }
      if (cli_eq(tokens[3], "up") != 0u) {
        up = 1u;
      } else if (cli_eq(tokens[3], "down") != 0u) {
        up = 0u;
      } else {
        return APP_EINVAL;
      }
      return cli_cmd_port_admin((uint8_t)p, up);
    }
    if (cli_eq(tokens[1], "mode") != 0u) {
      uint32_t p = 0u;
      uint32_t speed = 0u;
      uint8_t full_duplex = 0u;
      if (ntok < 5u) {
        return APP_EINVAL;
      }
      if (cli_parse_u32(tokens[2], &p) != APP_OK) {
        return APP_EINVAL;
      }
      if (cli_is_managed_port(p) == 0u) {
        return APP_EINVAL;
      }
      if (cli_parse_u32(tokens[3], &speed) != APP_OK) {
        return APP_EINVAL;
      }
      if ((speed != LAN9645_CORE_PORT_SPEED_10_MBPS) && (speed != LAN9645_CORE_PORT_SPEED_100_MBPS)) {
        return APP_EINVAL;
      }
      if (cli_eq(tokens[4], "full") != 0u) {
        full_duplex = 1u;
      } else if (cli_eq(tokens[4], "half") != 0u) {
        full_duplex = 0u;
      } else {
        return APP_EINVAL;
      }
      return cli_cmd_port_mode((uint8_t)p, (uint16_t)speed, full_duplex);
    }
    return APP_EINVAL;
  }

  if (cli_eq(tokens[0], "vlan") != 0u) {
    uint32_t vid = 0u;
    if (ntok < 3u) {
      return APP_EINVAL;
    }
    if (cli_parse_u32(tokens[2], &vid) != APP_OK) {
      return APP_EINVAL;
    }
    if (vid > LAN9645_VLAN_VID_MAX) {
      return APP_EINVAL;
    }

    if (cli_eq(tokens[1], "show") != 0u) {
      return cli_cmd_vlan_show((uint16_t)vid);
    }
    if (cli_eq(tokens[1], "set") != 0u) {
      uint32_t member_mask = 0u;
      uint32_t untag_mask = 0u;
      if (ntok < 5u) {
        return APP_EINVAL;
      }
      if (cli_parse_u32(tokens[3], &member_mask) != APP_OK) {
        return APP_EINVAL;
      }
      if (cli_parse_u32(tokens[4], &untag_mask) != APP_OK) {
        return APP_EINVAL;
      }
      if ((member_mask & (uint32_t)(~LAN9645_VLAN_MEMBER_MASK_ALL)) != 0u) {
        return APP_EINVAL;
      }
      if ((untag_mask & (uint32_t)(~LAN9645_VLAN_MEMBER_MASK_ALL)) != 0u) {
        return APP_EINVAL;
      }
      return cli_cmd_vlan_set((uint16_t)vid, (uint16_t)member_mask, (uint16_t)untag_mask);
    }
    return APP_EINVAL;
  }

  if (cli_eq(tokens[0], "spi") != 0u) {
    if (ntok < 2u) {
      return APP_EINVAL;
    }
    if (cli_eq(tokens[1], "status") != 0u) {
      return cli_cmd_spi_status();
    }
    if (cli_eq(tokens[1], "stats") != 0u) {
      if (ntok == 2u) {
        return cli_cmd_spi_stats();
      }
      if ((ntok == 3u) && (cli_eq(tokens[2], "clear") != 0u)) {
        return cli_cmd_spi_stats_clear();
      }
      return APP_EINVAL;
    }
    return APP_EINVAL;
  }

  if (cli_eq(tokens[0], "eth") != 0u) {
    if ((ntok < 2u) || (cli_eq(tokens[1], "show") == 0u)) {
      return APP_EINVAL;
    }
    return cli_cmd_eth_show();
  }

  if (cli_eq(tokens[0], "net") != 0u) {
    if (ntok < 2u) {
      return APP_EINVAL;
    }
    if (cli_eq(tokens[1], "show") != 0u) {
      return cli_cmd_net_show();
    }
    if (cli_eq(tokens[1], "check") != 0u) {
      return cli_cmd_net_check();
    }
    return APP_EINVAL;
  }

  if (cli_eq(tokens[0], "irq") != 0u) {
    if (ntok < 2u) {
      return APP_EINVAL;
    }
    if (cli_eq(tokens[1], "show") != 0u) {
      return cli_cmd_irq_show();
    }
    if (cli_eq(tokens[1], "clear") != 0u) {
      return cli_cmd_irq_clear();
    }
    if (cli_eq(tokens[1], "ack") != 0u) {
      return cli_cmd_irq_ack();
    }
    return APP_EINVAL;
  }

  if (cli_eq(tokens[0], "serial") != 0u) {
    if ((ntok < 2u) || (cli_eq(tokens[1], "show") == 0u)) {
      return APP_EINVAL;
    }
    return cli_cmd_serial_show();
  }

  if (cli_eq(tokens[0], "reg") != 0u) {
    uint32_t addr = 0u;
    uint32_t value = 0u;

    if (ntok < 3u) {
      return APP_EINVAL;
    }
    if (cli_parse_u32(tokens[2], &addr) != APP_OK) {
      return APP_EINVAL;
    }

    if (cli_eq(tokens[1], "read") != 0u) {
      return cli_cmd_reg_read(addr);
    }
    if (cli_eq(tokens[1], "write") != 0u) {
      if (ntok < 4u) {
        return APP_EINVAL;
      }
      if (cli_parse_u32(tokens[3], &value) != APP_OK) {
        return APP_EINVAL;
      }
      return cli_cmd_reg_write(addr, value);
    }
    return APP_EINVAL;
  }

  if (cli_eq(tokens[0], "sanity") != 0u) {
    uint32_t addr = 0u;
    uint32_t count = 100u;

    if ((ntok < 3u) || (cli_eq(tokens[1], "spi") == 0u)) {
      return APP_EINVAL;
    }
    if (cli_parse_u32(tokens[2], &addr) != APP_OK) {
      return APP_EINVAL;
    }
    if (ntok >= 4u) {
      if (cli_parse_u32(tokens[3], &count) != APP_OK) {
        return APP_EINVAL;
      }
    }

    return cli_cmd_sanity_spi(addr, count);
  }

  return APP_ENOTSUP;
}

/** @brief Initializes the CLI core and prints a ready banner. */
app_status_t cli_core_init(void)
{
  memset(&s_cli, 0, sizeof(s_cli));
  s_cli.initialized = 1u;
  s_cli.active_port = LAN9645_USER_PHY_PORT0;
  s_cli_phy_ready = 0u;
  (void)cli_writef("cli ready\r\n");
  return APP_OK;
}

/** @brief Placeholder periodic hook for future CLI background work. */
app_status_t cli_core_poll(void)
{
  if (s_cli.initialized == 0u) {
    (void)cli_core_init();
  }
  return APP_OK;
}

/** @brief Parses and executes one CLI line. */
app_status_t cli_core_exec_line(const char *line)
{
  char buf[CLI_MAX_LINE_LEN];
  char *tokens[CLI_MAX_TOKENS] = {0};
  char *tok = NULL;
  uint32_t ntok = 0u;
  app_status_t st = APP_OK;

  if (line == NULL) {
    return APP_EINVAL;
  }
  if (s_cli.initialized == 0u) {
    (void)cli_core_init();
  }

  (void)snprintf(buf, sizeof(buf), "%s", line);
  tok = strtok(buf, " \t\r\n");
  while ((tok != NULL) && (ntok < CLI_MAX_TOKENS)) {
    tokens[ntok++] = tok;
    tok = strtok(NULL, " \t\r\n");
  }

  if (ntok == 0u) {
    return APP_OK;
  }

  s_cli.command_count++;
  st = cli_exec_tokens(tokens, ntok);
  if (st == APP_OK) {
    return APP_OK;
  }

  s_cli.command_error_count++;
  (void)cli_writef("cmd error=%ld\r\n", (long)st);
  return st;
}

/** @brief Returns the current CLI counters and active-port selection. */
app_status_t cli_core_get_state(cli_core_state_t *out)
{
  if (out == NULL) {
    return APP_EINVAL;
  }
  if (s_cli.initialized == 0u) {
    (void)cli_core_init();
  }

  *out = s_cli;
  return APP_OK;
}
