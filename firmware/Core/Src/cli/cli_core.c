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
#include "lan9645/lan9645_feature_matrix.h"
#include "lan9645/lan9645_afi.h"
#include "lan9645/lan9645_bum.h"
#include "lan9645/lan9645_cbs.h"
#include "lan9645/lan9645_cut_thru.h"
#include "lan9645/lan9645_dcb.h"
#include "lan9645/lan9645_eee.h"
#include "lan9645/lan9645_fp.h"
#include "lan9645/lan9645_hsr_prp.h"
#include "lan9645/lan9645_lag.h"
#include "lan9645/lan9645_mac.h"
#include "lan9645/lan9645_manual_inj.h"
#include "lan9645/lan9645_mdb.h"
#include "lan9645/lan9645_mirror.h"
#include "lan9645/lan9645_mrp.h"
#include "lan9645/lan9645_npi.h"
#include "lan9645/lan9645_phylink.h"
#include "lan9645/lan9645_police.h"
#include "lan9645/lan9645_ptp.h"
#include "lan9645/lan9645_psfp.h"
#include "lan9645/lan9645_qos.h"
#include "lan9645/lan9645_tbf.h"
#include "lan9645/lan9645_taprio.h"
#include "lan9645/lan9645_stream.h"
#include "lan9645/lan9645_reg.h"
#include "lan9645/lan9645_regs.h"
#include "lan9645/lan9645_runtime.h"
#include "lan9645/lan9645_stats.h"
#include "lan9645/lan9645_tc_flower.h"
#include "lan9645/lan9645_tc_matchall.h"
#include "lan9645/lan9645_user_phy_adapter.h"
#include "lan9645/lan9645_vcap.h"
#include "lan9645/lan9645_vlan.h"
#include "lwip.h"

#define CLI_MAX_LINE_LEN      (160u)
#define CLI_MAX_TOKENS        (8u)
#define CLI_RESULT_SCHEMA_VERSION (1u)
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

/** @brief Converts an application status code into a stable symbolic token. */
static const char *cli_status_to_code(app_status_t st)
{
  switch (st) {
    case APP_OK: return "APP_OK";
    case APP_EINVAL: return "APP_EINVAL";
    case APP_EIO: return "APP_EIO";
    case APP_ETIMEOUT: return "APP_ETIMEOUT";
    case APP_ENOTSUP: return "APP_ENOTSUP";
    case APP_ESTATE: return "APP_ESTATE";
    default: return "APP_EUNKNOWN";
  }
}

/** @brief Emits one deterministic machine-readable command result envelope. */
static void cli_emit_result(app_status_t st)
{
  (void)cli_writef(
    "cli result=%s st=%ld code=%s seq=%lu schema=%u\r\n",
    (st == APP_OK) ? "ok" : "err",
    (long)st,
    cli_status_to_code(st),
    (unsigned long)s_cli.command_count,
    (unsigned int)CLI_RESULT_SCHEMA_VERSION
  );
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
    case DIAG_EVT_LAN_CNT_OP_FAIL: return "LAN_CNT_OP_FAIL";
    case DIAG_EVT_LAN_FEAT_CFG_FAIL: return "LAN_FEAT_CFG_FAIL";
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

/** @brief Converts in-band RGMII speed code to readable text. */
static const char *cli_phylink_speed_code_to_str(uint8_t speed_code)
{
  if (speed_code == 0u) {
    return "10";
  }
  if (speed_code == 1u) {
    return "100";
  }
  if (speed_code == 2u) {
    return "1000";
  }
  return "unknown";
}

/** @brief Converts LAN9645 IRQ analysis severity code into readable text. */
static const char *cli_irq_severity_to_str(uint8_t severity_code)
{
  switch (severity_code) {
    case LAN9645_CORE_IRQ_SEV_ERR: return "err";
    case LAN9645_CORE_IRQ_SEV_WARN: return "warn";
    case LAN9645_CORE_IRQ_SEV_INFO:
    default:
      break;
  }
  return "info";
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
  (void)cli_writef("  cli contract\r\n");
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
  (void)cli_writef("  cnt read <view>\r\n");
  (void)cli_writef("  cnt clear <view>\r\n");
  (void)cli_writef("  feat list\r\n");
  (void)cli_writef("  feat summary\r\n");
  (void)cli_writef("  feat show <name>\r\n");
  (void)cli_writef("  feat enable <name> <on|off>\r\n");
  (void)cli_writef("  feat apply <name|all>\r\n");
  (void)cli_writef("  afi show\r\n");
  (void)cli_writef("  afi set <0|1>\r\n");
  (void)cli_writef("  inj show <0..9>\r\n");
  (void)cli_writef("  inj set <0..9> <0..3>\r\n");
  (void)cli_writef("  eee show <0|1|8>\r\n");
  (void)cli_writef("  eee set <0|1|8> <0|1> <0..127> <0..127>\r\n");
  (void)cli_writef("  fp show <0|1|8>\r\n");
  (void)cli_writef("  fp set <0|1|8> <0|1> <0|1> <0|1> <0x00..0xFF> <0..3>\r\n");
  (void)cli_writef("  fp verify show <0|1|8>\r\n");
  (void)cli_writef("  fp verify set <0|1|8> <0|1> <0..255> <0..3>\r\n");
  (void)cli_writef("  fp frag show <0|1|8>\r\n");
  (void)cli_writef("  fp frag set <0|1|8> <0..3>\r\n");
  (void)cli_writef("  dcb show <0|1|8>\r\n");
  (void)cli_writef("  dcb trust <0|1|8> <0|1> <0|1>\r\n");
  (void)cli_writef("  dcb default <0|1|8> <0..7> <0|1>\r\n");
  (void)cli_writef("  dcb pfc <0|1|8> <0|1> <0x00..0xFF> <0x00..0xFF>\r\n");
  (void)cli_writef("  mac show <0|1|8>\r\n");
  (void)cli_writef("  mac set <0|1|8> <up|down> <10|100> <half|full>\r\n");
  (void)cli_writef("  mirror show\r\n");
  (void)cli_writef("  mirror set <ing_mask_hex> <egr_mask_hex>\r\n");
  (void)cli_writef("  mirror src show <0|1|8>\r\n");
  (void)cli_writef("  mirror src set <0|1|8> <0|1>\r\n");
  (void)cli_writef("  bum flood show <0..7>\r\n");
  (void)cli_writef("  bum flood set <0..7> <0..63> <0..63> <0..63>\r\n");
  (void)cli_writef("  bum ipmc show\r\n");
  (void)cli_writef("  bum ipmc set <0..63> <0..63> <0..63> <0..63>\r\n");
  (void)cli_writef("  bum pgid show <0..63>\r\n");
  (void)cli_writef("  bum pgid set <0..63> <member_mask_hex>\r\n");
  (void)cli_writef("  cut show <0|1|8>\r\n");
  (void)cli_writef("  cut set <0|1|8> <0..7> <0x00..0xFF>\r\n");
  (void)cli_writef("  tbf qshow <0|1|8> <0..7>\r\n");
  (void)cli_writef("  tbf qset <0|1|8> <0..7> <0|1> <0..3> <0..32767> <0..63>\r\n");
  (void)cli_writef("  tbf pshow <0|1|8>\r\n");
  (void)cli_writef("  tbf pset <0|1|8> <0|1> <0..3> <0..32767> <0..63>\r\n");
  (void)cli_writef("  cbs qshow <0|1|8> <0..7>\r\n");
  (void)cli_writef("  cbs qset <0|1|8> <0..7> <0|1> <0..3> <0..32767> <0..63>\r\n");
  (void)cli_writef("  taprio show\r\n");
  (void)cli_writef("  taprio set <0..255> <0..31> <0|1>\r\n");
  (void)cli_writef("  taprio gate show <0|1|8>\r\n");
  (void)cli_writef("  taprio gate set <0|1|8> <0x00..0xFF>\r\n");
  (void)cli_writef("  taprio list show <0..31>\r\n");
  (void)cli_writef("  taprio list set <0..31> <0..7>\r\n");
  (void)cli_writef("  police pshow <0|1|8>\r\n");
  (void)cli_writef("  police pset <0|1|8> <0|1> <0|1> <0|1> <0x00..0xFF> <0..511>\r\n");
  (void)cli_writef("  police ishow <0..9>\r\n");
  (void)cli_writef("  police imode <0..9> <0|1> <0|1> <0..3> <0|1> <0|1>\r\n");
  (void)cli_writef("  police iopt <0..9> <0..31> <0|1>\r\n");
  (void)cli_writef("  police ipir <0..9> <0..4194303> <0..32767> <0..63>\r\n");
  (void)cli_writef("  stream show <0..127>\r\n");
  (void)cli_writef("  stream set <0..127> <0|1> <0|1> <0|1> <0..65535>\r\n");
  (void)cli_writef("  psfp sfid show <0..127>\r\n");
  (void)cli_writef("  psfp sfid set <0..127> <0|1> <0|1> <0|1> <0..65535>\r\n");
  (void)cli_writef("  psfp gate show <0..127>\r\n");
  (void)cli_writef("  psfp gate set <0..127> <0..7> <0|1> <0..15> <0|1>\r\n");
  (void)cli_writef("  psfp gate commit <0..127>\r\n");
  (void)cli_writef("  mqprio show <0|1|8>\r\n");
  (void)cli_writef("  mqprio mode <0|1|8> <strict|dwrr>\r\n");
  (void)cli_writef("  mqprio weight <0|1|8> <0..7> <0..31>\r\n");
  (void)cli_writef("  ets show <0|1|8>\r\n");
  (void)cli_writef("  ets mode <0|1|8> <strict|dwrr>\r\n");
  (void)cli_writef("  ets weight <0|1|8> <0..7> <0..31>\r\n");
  (void)cli_writef("  qos show <0|1|8>\r\n");
  (void)cli_writef("  qos trust <0|1|8> <none|pcp|dscp|both>\r\n");
  (void)cli_writef("  qos default <0|1|8> <0..7> <0|1>\r\n");
  (void)cli_writef("  qos dscp show <0..63>\r\n");
  (void)cli_writef("  qos dscp set <0..63> <0..7> <0|1> <0|1> <0|1>\r\n");
  (void)cli_writef("  qos pcpdei show <0|1|8> <0..7> <0|1>\r\n");
  (void)cli_writef("  qos pcpdei set <0|1|8> <0..7> <0|1> <0..7> <0|1> <0..7> <0|1>\r\n");
  (void)cli_writef("  qos rew show <0|1|8>\r\n");
  (void)cli_writef("  qos rew set <0|1|8> <0..3> <0..3>\r\n");
  (void)cli_writef("  qos tagmode show <0|1|8>\r\n");
  (void)cli_writef("  qos tagmode set <0|1|8> <0..3>\r\n");
  (void)cli_writef("  qos rewport show <0|1|8>\r\n");
  (void)cli_writef("  qos rewport set <0|1|8> <0..7> <0|1>\r\n");
  (void)cli_writef("  phylink show <0|1>\r\n");
  (void)cli_writef("  phylink set <0|1> <0|1> <0|1> <0|1> <0..7> <0|1> <0|1>\r\n");
  (void)cli_writef("  phylink cpu show <0|1|8>\r\n");
  (void)cli_writef("  phylink cpu set <0|1|8> <0|1> <10|100> <0|1>\r\n");
  (void)cli_writef("  npi show\r\n");
  (void)cli_writef("  npi ext set <0..9> <0x00..0xFF> <0|1> <0|1>\r\n");
  (void)cli_writef("  npi ifh set <0..3> <0..3>\r\n");
  (void)cli_writef("  npi rew set <0..3>\r\n");
  (void)cli_writef("  npi srcpgid set <0x000..0x3FF>\r\n");
  (void)cli_writef("  hsr_prp show\r\n");
  (void)cli_writef("  hsr_prp set <pgid_cpu_mask_hex>\r\n");
  (void)cli_writef("  lag show\r\n");
  (void)cli_writef("  lag set <aggr_mask_hex>\r\n");
  (void)cli_writef("  mdb cfg show <0..88>\r\n");
  (void)cli_writef("  mdb cfg set <0..88> <0|1> <0..7> <0|1>\r\n");
  (void)cli_writef("  mdb mask show <0..88>\r\n");
  (void)cli_writef("  mdb mask set <0..88> <member_mask_hex>\r\n");
  (void)cli_writef("  mdb ipmc show\r\n");
  (void)cli_writef("  mdb ipmc set <mcipv4_mask_hex> <mcipv6_mask_hex>\r\n");
  (void)cli_writef("  mrp show\r\n");
  (void)cli_writef("  mrp set <member_mask_hex>\r\n");
  (void)cli_writef("  ptp show\r\n");
  (void)cli_writef("  ptp global <0|1>\r\n");
  (void)cli_writef("  ptp wait <0|1|8> <0|1>\r\n");
  (void)cli_writef("  vcap show <0|1|8>\r\n");
  (void)cli_writef("  vcap set <0|1|8> <s1_raw_hex> <s2_raw_hex>\r\n");
  (void)cli_writef("  tcflower show <0|1|8>\r\n");
  (void)cli_writef("  tcflower set <0|1|8> <0|1>\r\n");
  (void)cli_writef("  tcmatch show <0|1|8>\r\n");
  (void)cli_writef("  tcmatch set <0|1|8> <0|1>\r\n");
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

/** @brief Prints the locked CLI result-envelope schema used for automation parsing. */
static app_status_t cli_cmd_contract_show(void)
{
  (void)cli_writef(
    "cli contract schema=%u format=\"cli result=<ok|err> st=<int> code=<symbol> seq=<u32> schema=<u32>\"\r\n",
    (unsigned int)CLI_RESULT_SCHEMA_VERSION
  );
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

/** @brief Converts scheduler mode enum to stable text for CLI reporting. */
static const char *cli_qos_sched_mode_to_str(lan9645_qos_sched_mode_t mode)
{
  if (mode == LAN9645_QOS_SCHED_DWRR) {
    return "dwrr";
  }
  return "strict";
}

/** @brief Prints scheduler state for one managed port in a specific domain view. */
static app_status_t cli_cmd_sched_show_domain(const char *domain, uint8_t port)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_qos_sched_state_t state = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL) || (domain == NULL)) {
    return APP_ESTATE;
  }
  if (cli_is_managed_port((uint32_t)port) == 0u) {
    return APP_EINVAL;
  }

  st = lan9645_qos_get_scheduler_state(core, port, &state);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "%s sched port=%u mode=%s rr=%u dwrr_cnt=%u cost=[%u,%u,%u,%u,%u,%u,%u,%u]\r\n",
    domain,
    (unsigned int)port,
    cli_qos_sched_mode_to_str(state.mode),
    (unsigned int)state.rr_enable,
    (unsigned int)state.dwrr_count,
    (unsigned int)state.cost[0],
    (unsigned int)state.cost[1],
    (unsigned int)state.cost[2],
    (unsigned int)state.cost[3],
    (unsigned int)state.cost[4],
    (unsigned int)state.cost[5],
    (unsigned int)state.cost[6],
    (unsigned int)state.cost[7]
  );

  return APP_OK;
}

/** @brief Applies scheduler mode for one managed port in a specific domain view. */
static app_status_t cli_cmd_sched_set_domain(
  const char *domain,
  uint8_t port,
  lan9645_qos_sched_mode_t mode
)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL) || (domain == NULL)) {
    return APP_ESTATE;
  }
  if (cli_is_managed_port((uint32_t)port) == 0u) {
    return APP_EINVAL;
  }

  st = lan9645_qos_set_scheduler_mode(core, port, mode);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "%s mode port=%u mode=%s\r\n",
    domain,
    (unsigned int)port,
    cli_qos_sched_mode_to_str(mode)
  );
  return cli_cmd_sched_show_domain(domain, port);
}

/** @brief Applies one DWRR queue weight for a managed port and prints readback state. */
static app_status_t cli_cmd_sched_weight_domain(
  const char *domain,
  uint8_t port,
  uint8_t queue,
  uint8_t cost
)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  uint8_t rd_cost = 0u;
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL) || (domain == NULL)) {
    return APP_ESTATE;
  }
  if ((cli_is_managed_port((uint32_t)port) == 0u) ||
      (queue >= LAN9645_QOS_SCHED_QUEUE_COUNT) ||
      (cost > LAN9645_QOS_DWRR_COST_MAX)) {
    return APP_EINVAL;
  }

  st = lan9645_qos_set_scheduler_weight(core, port, queue, cost);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_qos_get_scheduler_weight(core, port, queue, &rd_cost);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "%s weight port=%u queue=%u cost=%u readback=%u\r\n",
    domain,
    (unsigned int)port,
    (unsigned int)queue,
    (unsigned int)cost,
    (unsigned int)rd_cost
  );
  return cli_cmd_sched_show_domain(domain, port);
}

/** @brief Prints per-port QoS trust/default policy snapshot. */
static app_status_t cli_cmd_qos_show(uint8_t port)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_qos_port_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if (cli_is_managed_port((uint32_t)port) == 0u) {
    return APP_EINVAL;
  }

  st = lan9645_qos_get_port_cfg(core, port, &cfg);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "qos port=%u trust_pcp=%u trust_dscp=%u default_prio=%u default_dp=%u\r\n",
    (unsigned int)cfg.port,
    (unsigned int)cfg.trust_pcp,
    (unsigned int)cfg.trust_dscp,
    (unsigned int)cfg.default_prio,
    (unsigned int)cfg.default_dp
  );
  return APP_OK;
}

/** @brief Applies per-port QoS trust policy and prints readback snapshot. */
static app_status_t cli_cmd_qos_trust(uint8_t port, uint8_t trust_pcp, uint8_t trust_dscp)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if (cli_is_managed_port((uint32_t)port) == 0u) {
    return APP_EINVAL;
  }

  st = lan9645_qos_set_port_trust(core, port, trust_pcp, trust_dscp);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "qos trust port=%u pcp=%u dscp=%u\r\n",
    (unsigned int)port,
    (unsigned int)(trust_pcp != 0u),
    (unsigned int)(trust_dscp != 0u)
  );
  return cli_cmd_qos_show(port);
}

/** @brief Applies per-port QoS default values and prints readback snapshot. */
static app_status_t cli_cmd_qos_default(uint8_t port, uint8_t default_prio, uint8_t default_dp)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if (cli_is_managed_port((uint32_t)port) == 0u) {
    return APP_EINVAL;
  }
  if ((default_prio > LAN9645_QOS_DEFAULT_PRIO_MAX) || (default_dp > LAN9645_QOS_DEFAULT_DP_MAX)) {
    return APP_EINVAL;
  }

  st = lan9645_qos_set_port_default(core, port, default_prio, default_dp);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "qos default port=%u prio=%u dp=%u\r\n",
    (unsigned int)port,
    (unsigned int)default_prio,
    (unsigned int)default_dp
  );
  return cli_cmd_qos_show(port);
}

/** @brief Prints one global DSCP-map entry snapshot. */
static app_status_t cli_cmd_qos_dscp_show(uint8_t dscp)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_qos_dscp_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if (dscp > LAN9645_QOS_DSCP_MAX) {
    return APP_EINVAL;
  }

  st = lan9645_qos_get_dscp_entry(core, dscp, &cfg);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "qos dscp=%u prio=%u dp=%u trust=%u rewrite=%u\r\n",
    (unsigned int)cfg.dscp,
    (unsigned int)cfg.qos_prio,
    (unsigned int)cfg.dp,
    (unsigned int)cfg.trust_enable,
    (unsigned int)cfg.rewrite_enable
  );
  return APP_OK;
}

/** @brief Sets one global DSCP-map entry and prints readback snapshot. */
static app_status_t cli_cmd_qos_dscp_set(
  uint8_t dscp,
  uint8_t qos_prio,
  uint8_t dp,
  uint8_t trust_enable,
  uint8_t rewrite_enable
)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if ((dscp > LAN9645_QOS_DSCP_MAX) ||
      (qos_prio > LAN9645_QOS_DEFAULT_PRIO_MAX) ||
      (dp > LAN9645_QOS_DEFAULT_DP_MAX) ||
      (trust_enable > 1u) ||
      (rewrite_enable > 1u)) {
    return APP_EINVAL;
  }

  st = lan9645_qos_set_dscp_entry(core, dscp, qos_prio, dp, trust_enable, rewrite_enable);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "qos dscp set dscp=%u prio=%u dp=%u trust=%u rewrite=%u\r\n",
    (unsigned int)dscp,
    (unsigned int)qos_prio,
    (unsigned int)dp,
    (unsigned int)trust_enable,
    (unsigned int)rewrite_enable
  );
  return cli_cmd_qos_dscp_show(dscp);
}

/** @brief Prints one per-port PCP/DEI map entry snapshot. */
static app_status_t cli_cmd_qos_pcpdei_show(uint8_t port, uint8_t pcp, uint8_t dei)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_qos_pcpdei_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if ((cli_is_managed_port((uint32_t)port) == 0u) ||
      (pcp > LAN9645_QOS_PCP_MAX) ||
      (dei > LAN9645_QOS_DEI_MAX)) {
    return APP_EINVAL;
  }

  st = lan9645_qos_get_pcpdei_entry(core, port, pcp, dei, &cfg);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "qos pcpdei port=%u pcp=%u dei=%u prio=%u dp=%u rew_pcp=%u rew_dei=%u\r\n",
    (unsigned int)cfg.port,
    (unsigned int)cfg.pcp,
    (unsigned int)cfg.dei,
    (unsigned int)cfg.qos_prio,
    (unsigned int)cfg.dp,
    (unsigned int)cfg.rew_pcp,
    (unsigned int)cfg.rew_dei
  );
  return APP_OK;
}

/** @brief Sets one per-port PCP/DEI map entry and prints readback snapshot. */
static app_status_t cli_cmd_qos_pcpdei_set(
  uint8_t port,
  uint8_t pcp,
  uint8_t dei,
  uint8_t qos_prio,
  uint8_t dp,
  uint8_t rew_pcp,
  uint8_t rew_dei
)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if ((cli_is_managed_port((uint32_t)port) == 0u) ||
      (pcp > LAN9645_QOS_PCP_MAX) ||
      (dei > LAN9645_QOS_DEI_MAX) ||
      (qos_prio > LAN9645_QOS_DEFAULT_PRIO_MAX) ||
      (dp > LAN9645_QOS_DEFAULT_DP_MAX) ||
      (rew_pcp > LAN9645_QOS_PCP_MAX) ||
      (rew_dei > LAN9645_QOS_DEI_MAX)) {
    return APP_EINVAL;
  }

  st = lan9645_qos_set_pcpdei_entry(core, port, pcp, dei, qos_prio, dp, rew_pcp, rew_dei);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "qos pcpdei set port=%u pcp=%u dei=%u prio=%u dp=%u rew_pcp=%u rew_dei=%u\r\n",
    (unsigned int)port,
    (unsigned int)pcp,
    (unsigned int)dei,
    (unsigned int)qos_prio,
    (unsigned int)dp,
    (unsigned int)rew_pcp,
    (unsigned int)rew_dei
  );
  return cli_cmd_qos_pcpdei_show(port, pcp, dei);
}

/** @brief Prints per-port REW_TAG policy snapshot. */
static app_status_t cli_cmd_qos_rew_show(uint8_t port)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_qos_rew_tag_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if (cli_is_managed_port((uint32_t)port) == 0u) {
    return APP_EINVAL;
  }

  st = lan9645_qos_get_rew_tag_cfg(core, port, &cfg);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "qos rew port=%u tag_cfg=%u tag_pcp_cfg=%u tag_dei_cfg=%u\r\n",
    (unsigned int)cfg.port,
    (unsigned int)cfg.tag_cfg,
    (unsigned int)cfg.tag_pcp_cfg,
    (unsigned int)cfg.tag_dei_cfg
  );
  return APP_OK;
}

/** @brief Sets per-port REW_TAG policy and prints readback snapshot. */
static app_status_t cli_cmd_qos_rew_set(uint8_t port, uint8_t tag_pcp_cfg, uint8_t tag_dei_cfg)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if ((cli_is_managed_port((uint32_t)port) == 0u) ||
      (tag_pcp_cfg > LAN9645_QOS_REW_TAG_CFG_MAX) ||
      (tag_dei_cfg > LAN9645_QOS_REW_TAG_CFG_MAX)) {
    return APP_EINVAL;
  }

  st = lan9645_qos_set_rew_tag_cfg(core, port, tag_pcp_cfg, tag_dei_cfg);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "qos rew set port=%u tag_pcp_cfg=%u tag_dei_cfg=%u\r\n",
    (unsigned int)port,
    (unsigned int)tag_pcp_cfg,
    (unsigned int)tag_dei_cfg
  );
  return cli_cmd_qos_rew_show(port);
}

/** @brief Prints per-port REW_TAG tagmode snapshot. */
static app_status_t cli_cmd_qos_tagmode_show(uint8_t port)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_qos_rew_tag_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if (cli_is_managed_port((uint32_t)port) == 0u) {
    return APP_EINVAL;
  }

  st = lan9645_qos_get_rew_tag_cfg(core, port, &cfg);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "qos tagmode port=%u mode=%u\r\n",
    (unsigned int)cfg.port,
    (unsigned int)cfg.tag_cfg
  );
  return APP_OK;
}

/** @brief Sets per-port REW_TAG tagmode and prints readback snapshot. */
static app_status_t cli_cmd_qos_tagmode_set(uint8_t port, uint8_t tag_cfg)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if ((cli_is_managed_port((uint32_t)port) == 0u) ||
      (tag_cfg > LAN9645_QOS_REW_TAG_MODE_MAX)) {
    return APP_EINVAL;
  }

  st = lan9645_qos_set_rew_tag_mode(core, port, tag_cfg);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "qos tagmode set port=%u mode=%u\r\n",
    (unsigned int)port,
    (unsigned int)tag_cfg
  );
  return cli_cmd_qos_tagmode_show(port);
}

/** @brief Prints per-port REW_PORT_VLAN default PCP/DEI snapshot. */
static app_status_t cli_cmd_qos_rewport_show(uint8_t port)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_qos_rew_port_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if (cli_is_managed_port((uint32_t)port) == 0u) {
    return APP_EINVAL;
  }

  st = lan9645_qos_get_rew_port_cfg(core, port, &cfg);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "qos rewport port=%u pcp=%u dei=%u\r\n",
    (unsigned int)cfg.port,
    (unsigned int)cfg.port_pcp,
    (unsigned int)cfg.port_dei
  );
  return APP_OK;
}

/** @brief Sets per-port REW_PORT_VLAN default PCP/DEI and prints readback snapshot. */
static app_status_t cli_cmd_qos_rewport_set(uint8_t port, uint8_t port_pcp, uint8_t port_dei)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if ((cli_is_managed_port((uint32_t)port) == 0u) ||
      (port_pcp > LAN9645_QOS_PCP_MAX) ||
      (port_dei > LAN9645_QOS_DEI_MAX)) {
    return APP_EINVAL;
  }

  st = lan9645_qos_set_rew_port_cfg(core, port, port_pcp, port_dei);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "qos rewport set port=%u pcp=%u dei=%u\r\n",
    (unsigned int)port,
    (unsigned int)port_pcp,
    (unsigned int)port_dei
  );
  return cli_cmd_qos_rewport_show(port);
}

/** @brief Prints one EEE configuration snapshot for a managed port. */
static app_status_t cli_cmd_eee_show(uint8_t port)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_eee_port_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if (cli_is_managed_port((uint32_t)port) == 0u) {
    return APP_EINVAL;
  }

  st = lan9645_eee_get_port_cfg(core, port, &cfg);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "eee port=%u ena=%u wakeup=%u holdoff=%u age=%u lpi=%u\r\n",
    (unsigned int)cfg.port,
    (unsigned int)cfg.eee_enable,
    (unsigned int)cfg.wakeup_timer,
    (unsigned int)cfg.holdoff_timer,
    (unsigned int)cfg.age_timer,
    (unsigned int)cfg.port_lpi
  );
  return APP_OK;
}

/** @brief Sets one managed-port EEE configuration and prints readback snapshot. */
static app_status_t cli_cmd_eee_set(uint8_t port, uint8_t eee_enable, uint8_t wakeup_timer, uint8_t holdoff_timer)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if ((cli_is_managed_port((uint32_t)port) == 0u) ||
      (eee_enable > 1u) ||
      (wakeup_timer > LAN9645_EEE_WAKEUP_TIMER_MAX) ||
      (holdoff_timer > LAN9645_EEE_HOLDOFF_TIMER_MAX)) {
    return APP_EINVAL;
  }

  st = lan9645_eee_set_port_cfg(core, port, eee_enable, wakeup_timer, holdoff_timer);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "eee set port=%u ena=%u wakeup=%u holdoff=%u\r\n",
    (unsigned int)port,
    (unsigned int)eee_enable,
    (unsigned int)wakeup_timer,
    (unsigned int)holdoff_timer
  );
  return cli_cmd_eee_show(port);
}

/** @brief Prints one FP/MM configuration snapshot for a managed port. */
static app_status_t cli_cmd_fp_show(uint8_t port)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_fp_port_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if (cli_is_managed_port((uint32_t)port) == 0u) {
    return APP_EINVAL;
  }

  st = lan9645_fp_get_port_cfg(core, port, &cfg);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "fp port=%u mmrx=%u mmtx=%u keep=%u pqueues=0x%02X strict_ipg=%u ver_dis=%u ver_time=%u ver_units=%u frag=%u\r\n",
    (unsigned int)cfg.port,
    (unsigned int)cfg.mm_rx_enable,
    (unsigned int)cfg.mm_tx_enable,
    (unsigned int)cfg.keep_s_after_d,
    (unsigned int)cfg.preempt_queue_mask,
    (unsigned int)cfg.strict_ipg,
    (unsigned int)cfg.verify_disable,
    (unsigned int)cfg.verify_time,
    (unsigned int)cfg.verify_timer_units,
    (unsigned int)cfg.add_frag_size
  );
  return APP_OK;
}

/** @brief Sets main FP/MM control fields for one managed port and prints readback snapshot. */
static app_status_t cli_cmd_fp_set(
  uint8_t port,
  uint8_t mm_rx_enable,
  uint8_t mm_tx_enable,
  uint8_t keep_s_after_d,
  uint8_t preempt_queue_mask,
  uint8_t strict_ipg
)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_fp_port_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if ((cli_is_managed_port((uint32_t)port) == 0u) ||
      (mm_rx_enable > 1u) ||
      (mm_tx_enable > 1u) ||
      (keep_s_after_d > 1u) ||
      (preempt_queue_mask > LAN9645_FP_PREEMPT_QUEUE_MASK_MAX) ||
      (strict_ipg > LAN9645_FP_STRICT_IPG_MAX)) {
    return APP_EINVAL;
  }

  st = lan9645_fp_get_port_cfg(core, port, &cfg);
  if (st != APP_OK) {
    return st;
  }

  cfg.mm_rx_enable = mm_rx_enable;
  cfg.mm_tx_enable = mm_tx_enable;
  cfg.keep_s_after_d = keep_s_after_d;
  cfg.preempt_queue_mask = preempt_queue_mask;
  cfg.strict_ipg = strict_ipg;

  st = lan9645_fp_set_port_cfg(core, port, &cfg);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "fp set port=%u mmrx=%u mmtx=%u keep=%u pqueues=0x%02X strict_ipg=%u\r\n",
    (unsigned int)port,
    (unsigned int)mm_rx_enable,
    (unsigned int)mm_tx_enable,
    (unsigned int)keep_s_after_d,
    (unsigned int)preempt_queue_mask,
    (unsigned int)strict_ipg
  );
  return cli_cmd_fp_show(port);
}

/** @brief Sets FP verification fields for one managed port and prints readback snapshot. */
static app_status_t cli_cmd_fp_verify_set(
  uint8_t port,
  uint8_t verify_disable,
  uint8_t verify_time,
  uint8_t verify_timer_units
)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_fp_port_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if ((cli_is_managed_port((uint32_t)port) == 0u) ||
      (verify_disable > 1u) ||
      (verify_time > LAN9645_FP_VERIFY_TIME_MAX) ||
      (verify_timer_units > LAN9645_FP_VERIFY_UNITS_MAX)) {
    return APP_EINVAL;
  }

  st = lan9645_fp_get_port_cfg(core, port, &cfg);
  if (st != APP_OK) {
    return st;
  }

  cfg.verify_disable = verify_disable;
  cfg.verify_time = verify_time;
  cfg.verify_timer_units = verify_timer_units;

  st = lan9645_fp_set_port_cfg(core, port, &cfg);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "fp verify set port=%u dis=%u time=%u units=%u\r\n",
    (unsigned int)port,
    (unsigned int)verify_disable,
    (unsigned int)verify_time,
    (unsigned int)verify_timer_units
  );
  return cli_cmd_fp_show(port);
}

/** @brief Sets FP additional fragment-size field for one managed port and prints readback snapshot. */
static app_status_t cli_cmd_fp_frag_set(uint8_t port, uint8_t add_frag_size)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_fp_port_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if ((cli_is_managed_port((uint32_t)port) == 0u) ||
      (add_frag_size > LAN9645_FP_ADD_FRAG_SIZE_MAX)) {
    return APP_EINVAL;
  }

  st = lan9645_fp_get_port_cfg(core, port, &cfg);
  if (st != APP_OK) {
    return st;
  }

  cfg.add_frag_size = add_frag_size;

  st = lan9645_fp_set_port_cfg(core, port, &cfg);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "fp frag set port=%u size=%u\r\n",
    (unsigned int)port,
    (unsigned int)add_frag_size
  );
  return cli_cmd_fp_show(port);
}

/** @brief Prints one DCB per-port configuration snapshot. */
static app_status_t cli_cmd_dcb_show(uint8_t port)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_dcb_port_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if (cli_is_managed_port((uint32_t)port) == 0u) {
    return APP_EINVAL;
  }

  st = lan9645_dcb_get_port_cfg(core, port, &cfg);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "dcb port=%u trust_pcp=%u trust_dscp=%u def_prio=%u def_dp=%u vlan_pfc=%u rx_pfc=0x%02X tx_pfc=0x%02X\r\n",
    (unsigned int)cfg.port,
    (unsigned int)cfg.trust_pcp,
    (unsigned int)cfg.trust_dscp,
    (unsigned int)cfg.default_prio,
    (unsigned int)cfg.default_dp,
    (unsigned int)cfg.vlan_pfc_enable,
    (unsigned int)cfg.rx_pfc_mask,
    (unsigned int)cfg.tx_pfc_mask
  );
  return APP_OK;
}

/** @brief Sets DCB trust bits and prints readback snapshot. */
static app_status_t cli_cmd_dcb_trust_set(uint8_t port, uint8_t trust_pcp, uint8_t trust_dscp)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_dcb_port_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if ((cli_is_managed_port((uint32_t)port) == 0u) ||
      (trust_pcp > 1u) ||
      (trust_dscp > 1u)) {
    return APP_EINVAL;
  }

  st = lan9645_dcb_get_port_cfg(core, port, &cfg);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_dcb_set_port_cfg(
    core,
    port,
    trust_pcp,
    trust_dscp,
    cfg.default_prio,
    cfg.default_dp,
    cfg.vlan_pfc_enable,
    cfg.rx_pfc_mask,
    cfg.tx_pfc_mask
  );
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef("dcb trust set port=%u pcp=%u dscp=%u\r\n", (unsigned int)port, (unsigned int)trust_pcp, (unsigned int)trust_dscp);
  return cli_cmd_dcb_show(port);
}

/** @brief Sets DCB default priority/DP and prints readback snapshot. */
static app_status_t cli_cmd_dcb_default_set(uint8_t port, uint8_t default_prio, uint8_t default_dp)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_dcb_port_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if ((cli_is_managed_port((uint32_t)port) == 0u) ||
      (default_prio > LAN9645_DCB_DEFAULT_PRIO_MAX) ||
      (default_dp > LAN9645_DCB_DEFAULT_DP_MAX)) {
    return APP_EINVAL;
  }

  st = lan9645_dcb_get_port_cfg(core, port, &cfg);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_dcb_set_port_cfg(
    core,
    port,
    cfg.trust_pcp,
    cfg.trust_dscp,
    default_prio,
    default_dp,
    cfg.vlan_pfc_enable,
    cfg.rx_pfc_mask,
    cfg.tx_pfc_mask
  );
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef("dcb default set port=%u prio=%u dp=%u\r\n", (unsigned int)port, (unsigned int)default_prio, (unsigned int)default_dp);
  return cli_cmd_dcb_show(port);
}

/** @brief Sets DCB PFC fields and prints readback snapshot. */
static app_status_t cli_cmd_dcb_pfc_set(uint8_t port, uint8_t vlan_pfc_enable, uint8_t rx_pfc_mask, uint8_t tx_pfc_mask)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_dcb_port_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if ((cli_is_managed_port((uint32_t)port) == 0u) ||
      (vlan_pfc_enable > 1u) ||
      (rx_pfc_mask > LAN9645_DCB_PFC_MASK_MAX) ||
      (tx_pfc_mask > LAN9645_DCB_PFC_MASK_MAX)) {
    return APP_EINVAL;
  }

  st = lan9645_dcb_get_port_cfg(core, port, &cfg);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_dcb_set_port_cfg(
    core,
    port,
    cfg.trust_pcp,
    cfg.trust_dscp,
    cfg.default_prio,
    cfg.default_dp,
    vlan_pfc_enable,
    rx_pfc_mask,
    tx_pfc_mask
  );
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "dcb pfc set port=%u vlan=%u rx=0x%02X tx=0x%02X\r\n",
    (unsigned int)port,
    (unsigned int)vlan_pfc_enable,
    (unsigned int)rx_pfc_mask,
    (unsigned int)tx_pfc_mask
  );
  return cli_cmd_dcb_show(port);
}

/** @brief Prints one MAC per-port configuration snapshot. */
static app_status_t cli_cmd_mac_show(uint8_t port)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_mac_port_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if (cli_is_managed_port((uint32_t)port) == 0u) {
    return APP_EINVAL;
  }

  st = lan9645_mac_get_port_cfg(core, port, &cfg);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "mac port=%u admin=%s speed=%s duplex=%s mac_rx=%u mac_tx=%u\r\n",
    (unsigned int)cfg.port,
    (cfg.admin_up != 0u) ? "up" : "down",
    cli_port_speed_to_str(cfg.speed_mbps),
    (cfg.full_duplex != 0u) ? "full" : "half",
    (unsigned int)cfg.mac_rx_ena,
    (unsigned int)cfg.mac_tx_ena
  );
  return APP_OK;
}

/** @brief Sets one MAC per-port configuration and prints readback snapshot. */
static app_status_t cli_cmd_mac_set(uint8_t port, uint8_t admin_up, uint16_t speed_mbps, uint8_t full_duplex)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if ((cli_is_managed_port((uint32_t)port) == 0u) ||
      (admin_up > 1u) ||
      (full_duplex > 1u) ||
      ((speed_mbps != LAN9645_CORE_PORT_SPEED_10_MBPS) &&
       (speed_mbps != LAN9645_CORE_PORT_SPEED_100_MBPS))) {
    return APP_EINVAL;
  }

  st = lan9645_mac_set_port_cfg(core, port, admin_up, speed_mbps, full_duplex);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "mac set port=%u admin=%s speed=%s duplex=%s\r\n",
    (unsigned int)port,
    (admin_up != 0u) ? "up" : "down",
    cli_port_speed_to_str(speed_mbps),
    (full_duplex != 0u) ? "full" : "half"
  );
  return cli_cmd_mac_show(port);
}

/** @brief Prints global and per-port mirror configuration snapshot. */
static app_status_t cli_cmd_mirror_show(void)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  uint16_t ingress_mask = 0u;
  uint16_t egress_mask = 0u;
  const uint8_t ports[] = {0u, 1u, 8u};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }

  st = lan9645_mirror_get_global_cfg(core, &ingress_mask, &egress_mask);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "mirror global ingress=0x%03X egress=0x%03X\r\n",
    (unsigned int)ingress_mask,
    (unsigned int)egress_mask
  );

  for (uint32_t i = 0u; i < (uint32_t)(sizeof(ports) / sizeof(ports[0])); ++i) {
    uint8_t enable = 0u;
    st = lan9645_mirror_get_src_port(core, ports[i], &enable);
    if (st != APP_OK) {
      return st;
    }
    (void)cli_writef("mirror src port=%u ena=%u\r\n", (unsigned int)ports[i], (unsigned int)enable);
  }

  return APP_OK;
}

/** @brief Sets global mirror masks and prints readback snapshot. */
static app_status_t cli_cmd_mirror_set(uint16_t ingress_mask, uint16_t egress_mask)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if ((ingress_mask > LAN9645_MIRROR_PORT_MASK_MAX) ||
      (egress_mask > LAN9645_MIRROR_PORT_MASK_MAX)) {
    return APP_EINVAL;
  }

  st = lan9645_mirror_set_global_cfg(core, ingress_mask, egress_mask);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "mirror set ingress=0x%03X egress=0x%03X\r\n",
    (unsigned int)ingress_mask,
    (unsigned int)egress_mask
  );
  return cli_cmd_mirror_show();
}

/** @brief Prints ingress mirror-source enable state for one managed port. */
static app_status_t cli_cmd_mirror_src_show(uint8_t port)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  uint8_t enable = 0u;
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if (cli_is_managed_port((uint32_t)port) == 0u) {
    return APP_EINVAL;
  }

  st = lan9645_mirror_get_src_port(core, port, &enable);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef("mirror src port=%u ena=%u\r\n", (unsigned int)port, (unsigned int)enable);
  return APP_OK;
}

/** @brief Sets ingress mirror-source enable state for one managed port and prints readback snapshot. */
static app_status_t cli_cmd_mirror_src_set(uint8_t port, uint8_t enable)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if ((cli_is_managed_port((uint32_t)port) == 0u) || (enable > 1u)) {
    return APP_EINVAL;
  }

  st = lan9645_mirror_set_src_port(core, port, enable);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef("mirror src set port=%u ena=%u\r\n", (unsigned int)port, (unsigned int)enable);
  return cli_cmd_mirror_src_show(port);
}

/** @brief Prints one BUM flooding-priority snapshot. */
static app_status_t cli_cmd_bum_flood_show(uint8_t priority)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_bum_flood_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if (priority >= LAN9645_BUM_PRIO_COUNT) {
    return APP_EINVAL;
  }

  st = lan9645_bum_get_flood_cfg(core, priority, &cfg);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "bum flood prio=%u uc=%u bc=%u mc=%u\r\n",
    (unsigned int)cfg.priority,
    (unsigned int)cfg.pgid_uc,
    (unsigned int)cfg.pgid_bc,
    (unsigned int)cfg.pgid_mc
  );
  return APP_OK;
}

/** @brief Sets one BUM flooding-priority entry and prints readback snapshot. */
static app_status_t cli_cmd_bum_flood_set(uint8_t priority, uint8_t pgid_uc, uint8_t pgid_bc, uint8_t pgid_mc)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if ((priority >= LAN9645_BUM_PRIO_COUNT) ||
      (pgid_uc > LAN9645_BUM_PGID_MAX) ||
      (pgid_bc > LAN9645_BUM_PGID_MAX) ||
      (pgid_mc > LAN9645_BUM_PGID_MAX)) {
    return APP_EINVAL;
  }

  st = lan9645_bum_set_flood_cfg(core, priority, pgid_uc, pgid_bc, pgid_mc);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "bum flood set prio=%u uc=%u bc=%u mc=%u\r\n",
    (unsigned int)priority,
    (unsigned int)pgid_uc,
    (unsigned int)pgid_bc,
    (unsigned int)pgid_mc
  );
  return cli_cmd_bum_flood_show(priority);
}

/** @brief Prints BUM IPMC flooding configuration snapshot. */
static app_status_t cli_cmd_bum_ipmc_show(void)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_bum_ipmc_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }

  st = lan9645_bum_get_ipmc_cfg(core, &cfg);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "bum ipmc mc4_ctrl=%u mc4_data=%u mc6_ctrl=%u mc6_data=%u\r\n",
    (unsigned int)cfg.pgid_mc4_ctrl,
    (unsigned int)cfg.pgid_mc4_data,
    (unsigned int)cfg.pgid_mc6_ctrl,
    (unsigned int)cfg.pgid_mc6_data
  );
  return APP_OK;
}

/** @brief Sets BUM IPMC flooding configuration and prints readback snapshot. */
static app_status_t cli_cmd_bum_ipmc_set(
  uint8_t pgid_mc4_ctrl,
  uint8_t pgid_mc4_data,
  uint8_t pgid_mc6_ctrl,
  uint8_t pgid_mc6_data
)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if ((pgid_mc4_ctrl > LAN9645_BUM_PGID_MAX) ||
      (pgid_mc4_data > LAN9645_BUM_PGID_MAX) ||
      (pgid_mc6_ctrl > LAN9645_BUM_PGID_MAX) ||
      (pgid_mc6_data > LAN9645_BUM_PGID_MAX)) {
    return APP_EINVAL;
  }

  st = lan9645_bum_set_ipmc_cfg(core, pgid_mc4_ctrl, pgid_mc4_data, pgid_mc6_ctrl, pgid_mc6_data);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "bum ipmc set mc4_ctrl=%u mc4_data=%u mc6_ctrl=%u mc6_data=%u\r\n",
    (unsigned int)pgid_mc4_ctrl,
    (unsigned int)pgid_mc4_data,
    (unsigned int)pgid_mc6_ctrl,
    (unsigned int)pgid_mc6_data
  );
  return cli_cmd_bum_ipmc_show();
}

/** @brief Prints one BUM PGID destination member-mask snapshot. */
static app_status_t cli_cmd_bum_pgid_show(uint8_t pgid)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  uint16_t member_mask = 0u;
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if (pgid > LAN9645_BUM_PGID_MAX) {
    return APP_EINVAL;
  }

  st = lan9645_bum_get_pgid_mask(core, pgid, &member_mask);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef("bum pgid=%u member=0x%03X\r\n", (unsigned int)pgid, (unsigned int)member_mask);
  return APP_OK;
}

/** @brief Sets one BUM PGID destination member-mask and prints readback snapshot. */
static app_status_t cli_cmd_bum_pgid_set(uint8_t pgid, uint16_t member_mask)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if ((pgid > LAN9645_BUM_PGID_MAX) ||
      (member_mask > LAN9645_BUM_MEMBER_MASK_MAX)) {
    return APP_EINVAL;
  }

  st = lan9645_bum_set_pgid_mask(core, pgid, member_mask);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef("bum pgid set=%u member=0x%03X\r\n", (unsigned int)pgid, (unsigned int)member_mask);
  return cli_cmd_bum_pgid_show(pgid);
}

/** @brief Prints one cut-through configuration snapshot for a managed port. */
static app_status_t cli_cmd_cut_show(uint8_t port)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_cut_thru_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if (cli_is_managed_port((uint32_t)port) == 0u) {
    return APP_EINVAL;
  }

  st = lan9645_cut_thru_get_cfg(core, port, &cfg);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "cut port=%u spd=%u ena_mask=0x%02X\r\n",
    (unsigned int)cfg.port,
    (unsigned int)cfg.cut_thru_spd,
    (unsigned int)cfg.cut_thru_ena_mask
  );
  return APP_OK;
}

/** @brief Sets one managed-port cut-through configuration and prints readback snapshot. */
static app_status_t cli_cmd_cut_set(uint8_t port, uint8_t cut_thru_spd, uint8_t cut_thru_ena_mask)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if ((cli_is_managed_port((uint32_t)port) == 0u) ||
      (cut_thru_spd > LAN9645_CUT_THRU_SPD_MAX)) {
    return APP_EINVAL;
  }

  st = lan9645_cut_thru_set_cfg(core, port, cut_thru_spd, cut_thru_ena_mask);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "cut set port=%u spd=%u ena_mask=0x%02X\r\n",
    (unsigned int)port,
    (unsigned int)cut_thru_spd,
    (unsigned int)cut_thru_ena_mask
  );
  return cli_cmd_cut_show(port);
}

/** @brief Prints queue/port TBF scheduler-element snapshot for one managed port. */
static app_status_t cli_cmd_tbf_show(uint8_t port, lan9645_tbf_scope_t scope, uint8_t queue)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_tbf_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if (cli_is_managed_port((uint32_t)port) == 0u) {
    return APP_EINVAL;
  }
  if ((scope == LAN9645_TBF_SCOPE_QUEUE) && (queue >= LAN9645_TBF_QUEUE_COUNT)) {
    return APP_EINVAL;
  }

  st = lan9645_tbf_get_cfg(core, port, scope, queue, &cfg);
  if (st != APP_OK) {
    return st;
  }

  if (scope == LAN9645_TBF_SCOPE_QUEUE) {
    (void)cli_writef(
      "tbf queue port=%u queue=%u avb=%u frm=%u rate=%u burst=%u\r\n",
      (unsigned int)cfg.port,
      (unsigned int)cfg.queue,
      (unsigned int)cfg.avb_enable,
      (unsigned int)cfg.frm_mode,
      (unsigned int)cfg.cir_rate,
      (unsigned int)cfg.cir_burst
    );
  } else {
    (void)cli_writef(
      "tbf port port=%u avb=%u frm=%u rate=%u burst=%u\r\n",
      (unsigned int)cfg.port,
      (unsigned int)cfg.avb_enable,
      (unsigned int)cfg.frm_mode,
      (unsigned int)cfg.cir_rate,
      (unsigned int)cfg.cir_burst
    );
  }
  return APP_OK;
}

/** @brief Sets queue/port TBF scheduler-element configuration and prints readback snapshot. */
static app_status_t cli_cmd_tbf_set(
  uint8_t port,
  lan9645_tbf_scope_t scope,
  uint8_t queue,
  uint8_t avb_enable,
  uint8_t frm_mode,
  uint16_t cir_rate,
  uint8_t cir_burst
)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if (cli_is_managed_port((uint32_t)port) == 0u) {
    return APP_EINVAL;
  }

  st = lan9645_tbf_set_cfg(core, port, scope, queue, avb_enable, frm_mode, cir_rate, cir_burst);
  if (st != APP_OK) {
    return st;
  }

  if (scope == LAN9645_TBF_SCOPE_QUEUE) {
    (void)cli_writef(
      "tbf qset port=%u queue=%u avb=%u frm=%u rate=%u burst=%u\r\n",
      (unsigned int)port,
      (unsigned int)queue,
      (unsigned int)avb_enable,
      (unsigned int)frm_mode,
      (unsigned int)cir_rate,
      (unsigned int)cir_burst
    );
  } else {
    (void)cli_writef(
      "tbf pset port=%u avb=%u frm=%u rate=%u burst=%u\r\n",
      (unsigned int)port,
      (unsigned int)avb_enable,
      (unsigned int)frm_mode,
      (unsigned int)cir_rate,
      (unsigned int)cir_burst
    );
  }
  return cli_cmd_tbf_show(port, scope, queue);
}

/** @brief Prints one CBS queue scheduler-element snapshot for a managed port. */
static app_status_t cli_cmd_cbs_show(uint8_t port, uint8_t queue)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_cbs_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if ((cli_is_managed_port((uint32_t)port) == 0u) ||
      (queue >= LAN9645_CBS_QUEUE_COUNT)) {
    return APP_EINVAL;
  }

  st = lan9645_cbs_get_queue_cfg(core, port, queue, &cfg);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "cbs queue port=%u queue=%u avb=%u frm=%u rate=%u burst=%u\r\n",
    (unsigned int)cfg.port,
    (unsigned int)cfg.queue,
    (unsigned int)cfg.avb_enable,
    (unsigned int)cfg.frm_mode,
    (unsigned int)cfg.cir_rate,
    (unsigned int)cfg.cir_burst
  );
  return APP_OK;
}

/** @brief Sets one CBS queue scheduler-element and prints readback snapshot. */
static app_status_t cli_cmd_cbs_set(
  uint8_t port,
  uint8_t queue,
  uint8_t avb_enable,
  uint8_t frm_mode,
  uint16_t cir_rate,
  uint8_t cir_burst
)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if ((cli_is_managed_port((uint32_t)port) == 0u) ||
      (queue >= LAN9645_CBS_QUEUE_COUNT)) {
    return APP_EINVAL;
  }

  st = lan9645_cbs_set_queue_cfg(core, port, queue, avb_enable, frm_mode, cir_rate, cir_burst);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "cbs qset port=%u queue=%u avb=%u frm=%u rate=%u burst=%u\r\n",
    (unsigned int)port,
    (unsigned int)queue,
    (unsigned int)avb_enable,
    (unsigned int)frm_mode,
    (unsigned int)cir_rate,
    (unsigned int)cir_burst
  );
  return cli_cmd_cbs_show(port, queue);
}

/** @brief Prints global TAPRIO/TAS configuration snapshot. */
static app_status_t cli_cmd_taprio_show(void)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_taprio_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }

  st = lan9645_taprio_get_cfg(core, &cfg);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "taprio cfg revisit_dly=%u list_num_max=%u list_num=%u guard=%u\r\n",
    (unsigned int)cfg.revisit_dly,
    (unsigned int)cfg.list_num_max,
    (unsigned int)cfg.list_num_selected,
    (unsigned int)cfg.always_guard_band
  );
  return APP_OK;
}

/** @brief Sets global TAPRIO/TAS configuration and prints readback snapshot. */
static app_status_t cli_cmd_taprio_set(uint8_t revisit_dly, uint8_t list_num_max, uint8_t always_guard_band)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }

  st = lan9645_taprio_set_cfg(core, revisit_dly, list_num_max, always_guard_band);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "taprio set revisit_dly=%u list_num_max=%u guard=%u\r\n",
    (unsigned int)revisit_dly,
    (unsigned int)list_num_max,
    (unsigned int)always_guard_band
  );
  return cli_cmd_taprio_show();
}

/** @brief Prints one managed-port TAPRIO gate mask snapshot. */
static app_status_t cli_cmd_taprio_gate_show(uint8_t port)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  uint8_t gate_mask = 0u;
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if (cli_is_managed_port((uint32_t)port) == 0u) {
    return APP_EINVAL;
  }

  st = lan9645_taprio_get_port_gate_state(core, port, &gate_mask);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "taprio gate port=%u mask=0x%02X\r\n",
    (unsigned int)port,
    (unsigned int)gate_mask
  );
  return APP_OK;
}

/** @brief Sets one managed-port TAPRIO gate mask and prints readback snapshot. */
static app_status_t cli_cmd_taprio_gate_set(uint8_t port, uint8_t gate_mask)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if ((cli_is_managed_port((uint32_t)port) == 0u) ||
      (gate_mask > LAN9645_TAPRIO_GATE_MASK_MAX)) {
    return APP_EINVAL;
  }

  st = lan9645_taprio_set_port_gate_state(core, port, gate_mask);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "taprio gate set port=%u mask=0x%02X\r\n",
    (unsigned int)port,
    (unsigned int)gate_mask
  );
  return cli_cmd_taprio_gate_show(port);
}

/** @brief Prints one TAPRIO list state snapshot. */
static app_status_t cli_cmd_taprio_list_show(uint8_t list_num)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  uint8_t list_state = 0u;
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if (list_num > LAN9645_TAPRIO_LIST_NUM_MAX) {
    return APP_EINVAL;
  }

  st = lan9645_taprio_get_list_state(core, list_num, &list_state);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "taprio list num=%u state=%u\r\n",
    (unsigned int)list_num,
    (unsigned int)list_state
  );
  return APP_OK;
}

/** @brief Sets one TAPRIO list state and prints readback snapshot. */
static app_status_t cli_cmd_taprio_list_set(uint8_t list_num, uint8_t list_state)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if ((list_num > LAN9645_TAPRIO_LIST_NUM_MAX) ||
      (list_state > LAN9645_TAPRIO_LIST_STATE_MAX)) {
    return APP_EINVAL;
  }

  st = lan9645_taprio_set_list_state(core, list_num, list_state);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "taprio list set num=%u state=%u\r\n",
    (unsigned int)list_num,
    (unsigned int)list_state
  );
  return cli_cmd_taprio_list_show(list_num);
}

/** @brief Prints one per-port policer configuration snapshot. */
static app_status_t cli_cmd_police_port_show(uint8_t port)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_police_port_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if (cli_is_managed_port((uint32_t)port) == 0u) {
    return APP_EINVAL;
  }

  st = lan9645_police_get_port_cfg(core, port, &cfg);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "police port=%u redir8021=%u redirip=%u portena=%u qmask=0x%02X order=%u\r\n",
    (unsigned int)cfg.port,
    (unsigned int)cfg.cpu_redir_8021,
    (unsigned int)cfg.cpu_redir_ip,
    (unsigned int)cfg.port_pol_enable,
    (unsigned int)cfg.queue_pol_enable,
    (unsigned int)cfg.pol_order
  );
  return APP_OK;
}

/** @brief Sets one per-port policer configuration and prints readback snapshot. */
static app_status_t cli_cmd_police_port_set(
  uint8_t port,
  uint8_t redir8021,
  uint8_t redirip,
  uint8_t portena,
  uint8_t qmask,
  uint16_t order
)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if ((cli_is_managed_port((uint32_t)port) == 0u) ||
      (redir8021 > 1u) ||
      (redirip > 1u) ||
      (portena > 1u) ||
      (qmask > LAN9645_POLICE_QUEUE_MASK_MAX) ||
      (order > LAN9645_POLICE_ORDER_MAX)) {
    return APP_EINVAL;
  }

  st = lan9645_police_set_port_cfg(core, port, redir8021, redirip, portena, qmask, order);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "police pset port=%u redir8021=%u redirip=%u portena=%u qmask=0x%02X order=%u\r\n",
    (unsigned int)port,
    (unsigned int)redir8021,
    (unsigned int)redirip,
    (unsigned int)portena,
    (unsigned int)qmask,
    (unsigned int)order
  );
  return cli_cmd_police_port_show(port);
}

/** @brief Prints one policer-index mode/rate snapshot. */
static app_status_t cli_cmd_police_index_show(uint8_t pol_idx)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_police_index_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if (pol_idx >= LAN9645_POLICE_PORT_POL_COUNT) {
    return APP_EINVAL;
  }

  st = lan9645_police_get_index_cfg(core, pol_idx, &cfg);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "police idx=%u drop=%u red=%u ipg=%u frm=%u dlb=%u cir=%u over=%u lvl=%lu rate=%u burst=%u\r\n",
    (unsigned int)cfg.pol_idx,
    (unsigned int)cfg.drop_on_yellow,
    (unsigned int)cfg.mark_all_red,
    (unsigned int)cfg.ipg_size,
    (unsigned int)cfg.frm_mode,
    (unsigned int)cfg.dlb_coupled,
    (unsigned int)cfg.cir_enable,
    (unsigned int)cfg.overshoot_enable,
    (unsigned long)cfg.pir_level,
    (unsigned int)cfg.pir_rate,
    (unsigned int)cfg.pir_burst
  );
  return APP_OK;
}

/** @brief Updates one policer-index mode subset and prints readback snapshot. */
static app_status_t cli_cmd_police_index_mode_set(
  uint8_t pol_idx,
  uint8_t drop,
  uint8_t mark_red,
  uint8_t frm_mode,
  uint8_t cir_enable,
  uint8_t overshoot_enable
)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_police_index_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if ((pol_idx >= LAN9645_POLICE_PORT_POL_COUNT) ||
      (drop > 1u) ||
      (mark_red > 1u) ||
      (frm_mode > LAN9645_POLICE_FRM_MODE_MAX) ||
      (cir_enable > 1u) ||
      (overshoot_enable > 1u)) {
    return APP_EINVAL;
  }

  st = lan9645_police_get_index_cfg(core, pol_idx, &cfg);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_police_set_index_cfg(
    core,
    pol_idx,
    drop,
    mark_red,
    cfg.ipg_size,
    frm_mode,
    cfg.dlb_coupled,
    cir_enable,
    overshoot_enable,
    cfg.pir_level,
    cfg.pir_rate,
    cfg.pir_burst
  );
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "police imode idx=%u drop=%u red=%u frm=%u cir=%u over=%u\r\n",
    (unsigned int)pol_idx,
    (unsigned int)drop,
    (unsigned int)mark_red,
    (unsigned int)frm_mode,
    (unsigned int)cir_enable,
    (unsigned int)overshoot_enable
  );
  return cli_cmd_police_index_show(pol_idx);
}

/** @brief Updates one policer-index option subset (`ipg`, `dlb`) and prints readback snapshot. */
static app_status_t cli_cmd_police_index_opt_set(uint8_t pol_idx, uint8_t ipg_size, uint8_t dlb_coupled)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_police_index_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if ((pol_idx >= LAN9645_POLICE_PORT_POL_COUNT) ||
      (ipg_size > LAN9645_POLICE_IPG_SIZE_MAX) ||
      (dlb_coupled > 1u)) {
    return APP_EINVAL;
  }

  st = lan9645_police_get_index_cfg(core, pol_idx, &cfg);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_police_set_index_cfg(
    core,
    pol_idx,
    cfg.drop_on_yellow,
    cfg.mark_all_red,
    ipg_size,
    cfg.frm_mode,
    dlb_coupled,
    cfg.cir_enable,
    cfg.overshoot_enable,
    cfg.pir_level,
    cfg.pir_rate,
    cfg.pir_burst
  );
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "police iopt idx=%u ipg=%u dlb=%u\r\n",
    (unsigned int)pol_idx,
    (unsigned int)ipg_size,
    (unsigned int)dlb_coupled
  );
  return cli_cmd_police_index_show(pol_idx);
}

/** @brief Updates one policer-index PIR tuple and prints readback snapshot. */
static app_status_t cli_cmd_police_index_pir_set(
  uint8_t pol_idx,
  uint32_t pir_level,
  uint16_t pir_rate,
  uint8_t pir_burst
)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_police_index_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if ((pol_idx >= LAN9645_POLICE_PORT_POL_COUNT) ||
      (pir_level > LAN9645_POLICE_PIR_LVL_MAX) ||
      (pir_rate > LAN9645_POLICE_PIR_RATE_MAX) ||
      (pir_burst > LAN9645_POLICE_PIR_BURST_MAX)) {
    return APP_EINVAL;
  }

  st = lan9645_police_get_index_cfg(core, pol_idx, &cfg);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_police_set_index_cfg(
    core,
    pol_idx,
    cfg.drop_on_yellow,
    cfg.mark_all_red,
    cfg.ipg_size,
    cfg.frm_mode,
    cfg.dlb_coupled,
    cfg.cir_enable,
    cfg.overshoot_enable,
    pir_level,
    pir_rate,
    pir_burst
  );
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "police ipir idx=%u lvl=%lu rate=%u burst=%u\r\n",
    (unsigned int)pol_idx,
    (unsigned long)pir_level,
    (unsigned int)pir_rate,
    (unsigned int)pir_burst
  );
  return cli_cmd_police_index_show(pol_idx);
}

/** @brief Prints one stream SFID entry snapshot. */
static app_status_t cli_cmd_stream_show(uint8_t sfid_index)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_stream_sfid_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if (sfid_index > LAN9645_STREAM_SFID_INDEX_MAX) {
    return APP_EINVAL;
  }

  st = lan9645_stream_get_sfid_cfg(core, sfid_index, &cfg);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "stream sfid=%u bo_frm=%u bo_ena=%u force_block=%u max_sdu=%u\r\n",
    (unsigned int)cfg.sfid_index,
    (unsigned int)cfg.bo_frm,
    (unsigned int)cfg.bo_frm_enable,
    (unsigned int)cfg.force_block,
    (unsigned int)cfg.max_sdu_len
  );
  return APP_OK;
}

/** @brief Sets one stream SFID entry and prints readback snapshot. */
static app_status_t cli_cmd_stream_set(
  uint8_t sfid_index,
  uint8_t bo_frm,
  uint8_t bo_frm_enable,
  uint8_t force_block,
  uint16_t max_sdu_len
)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if ((sfid_index > LAN9645_STREAM_SFID_INDEX_MAX) ||
      (bo_frm > 1u) ||
      (bo_frm_enable > 1u) ||
      (force_block > 1u) ||
      (max_sdu_len > LAN9645_STREAM_MAX_SDU_LEN_MAX)) {
    return APP_EINVAL;
  }

  st = lan9645_stream_set_sfid_cfg(
    core,
    sfid_index,
    bo_frm,
    bo_frm_enable,
    force_block,
    max_sdu_len
  );
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "stream set sfid=%u bo_frm=%u bo_ena=%u force_block=%u max_sdu=%u\r\n",
    (unsigned int)sfid_index,
    (unsigned int)bo_frm,
    (unsigned int)bo_frm_enable,
    (unsigned int)force_block,
    (unsigned int)max_sdu_len
  );
  return cli_cmd_stream_show(sfid_index);
}

/** @brief Prints one PSFP SFID entry snapshot. */
static app_status_t cli_cmd_psfp_sfid_show(uint8_t sfid_index)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_psfp_sfid_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if (sfid_index > LAN9645_PSFP_INDEX_MAX) {
    return APP_EINVAL;
  }

  st = lan9645_psfp_get_sfid_cfg(core, sfid_index, &cfg);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "psfp sfid=%u bo_frm=%u bo_ena=%u force_block=%u max_sdu=%u\r\n",
    (unsigned int)cfg.sfid_index,
    (unsigned int)cfg.bo_frm,
    (unsigned int)cfg.bo_frm_enable,
    (unsigned int)cfg.force_block,
    (unsigned int)cfg.max_sdu_len
  );
  return APP_OK;
}

/** @brief Sets one PSFP SFID entry and prints readback snapshot. */
static app_status_t cli_cmd_psfp_sfid_set(
  uint8_t sfid_index,
  uint8_t bo_frm,
  uint8_t bo_frm_enable,
  uint8_t force_block,
  uint16_t max_sdu_len
)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if ((sfid_index > LAN9645_PSFP_INDEX_MAX) ||
      (bo_frm > 1u) ||
      (bo_frm_enable > 1u) ||
      (force_block > 1u) ||
      (max_sdu_len > LAN9645_PSFP_MAX_SDU_LEN_MAX)) {
    return APP_EINVAL;
  }

  st = lan9645_psfp_set_sfid_cfg(core, sfid_index, bo_frm, bo_frm_enable, force_block, max_sdu_len);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "psfp sfid set idx=%u bo_frm=%u bo_ena=%u force_block=%u max_sdu=%u\r\n",
    (unsigned int)sfid_index,
    (unsigned int)bo_frm,
    (unsigned int)bo_frm_enable,
    (unsigned int)force_block,
    (unsigned int)max_sdu_len
  );
  return cli_cmd_psfp_sfid_show(sfid_index);
}

/** @brief Prints one PSFP gate base configuration snapshot. */
static app_status_t cli_cmd_psfp_gate_show(uint8_t sgid)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_psfp_gate_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if (sgid > LAN9645_PSFP_INDEX_MAX) {
    return APP_EINVAL;
  }

  st = lan9645_psfp_get_gate_cfg(core, sgid, &cfg);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "psfp gate sgid=%u list_len=%u gate=%u init_ips=%u init_gate=%u\r\n",
    (unsigned int)cfg.sgid,
    (unsigned int)cfg.list_length,
    (unsigned int)cfg.gate_enable,
    (unsigned int)cfg.init_ips,
    (unsigned int)cfg.init_gate_state
  );
  return APP_OK;
}

/** @brief Sets one PSFP gate base configuration and prints readback snapshot. */
static app_status_t cli_cmd_psfp_gate_set(
  uint8_t sgid,
  uint8_t list_length,
  uint8_t gate_enable,
  uint8_t init_ips,
  uint8_t init_gate_state
)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if ((sgid > LAN9645_PSFP_INDEX_MAX) ||
      (list_length > LAN9645_PSFP_LIST_LENGTH_MAX) ||
      (gate_enable > 1u) ||
      (init_ips > LAN9645_PSFP_INIT_IPS_MAX) ||
      (init_gate_state > 1u)) {
    return APP_EINVAL;
  }

  st = lan9645_psfp_set_gate_cfg(core, sgid, list_length, gate_enable, init_ips, init_gate_state);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "psfp gate set sgid=%u list_len=%u gate=%u init_ips=%u init_gate=%u\r\n",
    (unsigned int)sgid,
    (unsigned int)list_length,
    (unsigned int)gate_enable,
    (unsigned int)init_ips,
    (unsigned int)init_gate_state
  );
  return cli_cmd_psfp_gate_show(sgid);
}

/** @brief Commits one PSFP gate configuration and confirms by readback snapshot. */
static app_status_t cli_cmd_psfp_gate_commit(uint8_t sgid)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if (sgid > LAN9645_PSFP_INDEX_MAX) {
    return APP_EINVAL;
  }

  st = lan9645_psfp_commit_gate_cfg(core, sgid);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef("psfp gate commit sgid=%u\r\n", (unsigned int)sgid);
  return cli_cmd_psfp_gate_show(sgid);
}

/** @brief Prints one phylink RGMII configuration/status snapshot. */
static app_status_t cli_cmd_phylink_show(uint8_t rgmii_idx)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_phylink_rgmii_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if (rgmii_idx > LAN9645_PHYLINK_RGMII_IDX_MAX) {
    return APP_EINVAL;
  }

  st = lan9645_phylink_get_rgmii_cfg(core, rgmii_idx, &cfg);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "phylink rgmii=%u ib=%u ib_rx=%u ib_tx=%u tx_clk_cfg=%u rx_rst=%u tx_rst=%u link=%u duplex=%u speed_code=%u speed=%s\r\n",
    (unsigned int)cfg.rgmii_idx,
    (unsigned int)cfg.ib_enable,
    (unsigned int)cfg.ib_rx_enable,
    (unsigned int)cfg.ib_tx_enable,
    (unsigned int)cfg.tx_clk_cfg,
    (unsigned int)cfg.rgmii_rx_rst,
    (unsigned int)cfg.rgmii_tx_rst,
    (unsigned int)cfg.link_up,
    (unsigned int)cfg.duplex,
    (unsigned int)cfg.speed_code,
    cli_phylink_speed_code_to_str(cfg.speed_code)
  );
  return APP_OK;
}

/** @brief Applies one phylink RGMII configuration and prints readback state. */
static app_status_t cli_cmd_phylink_set(
  uint8_t rgmii_idx,
  uint8_t ib_enable,
  uint8_t ib_rx_enable,
  uint8_t ib_tx_enable,
  uint8_t tx_clk_cfg,
  uint8_t rgmii_rx_rst,
  uint8_t rgmii_tx_rst
)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if ((rgmii_idx > LAN9645_PHYLINK_RGMII_IDX_MAX) ||
      (ib_enable > 1u) ||
      (ib_rx_enable > 1u) ||
      (ib_tx_enable > 1u) ||
      (tx_clk_cfg > LAN9645_PHYLINK_TX_CLK_CFG_MAX) ||
      (rgmii_rx_rst > 1u) ||
      (rgmii_tx_rst > 1u)) {
    return APP_EINVAL;
  }

  st = lan9645_phylink_set_rgmii_cfg(
    core,
    rgmii_idx,
    ib_enable,
    ib_rx_enable,
    ib_tx_enable,
    tx_clk_cfg,
    rgmii_rx_rst,
    rgmii_tx_rst
  );
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "phylink set rgmii=%u ib=%u ib_rx=%u ib_tx=%u tx_clk_cfg=%u rx_rst=%u tx_rst=%u\r\n",
    (unsigned int)rgmii_idx,
    (unsigned int)ib_enable,
    (unsigned int)ib_rx_enable,
    (unsigned int)ib_tx_enable,
    (unsigned int)tx_clk_cfg,
    (unsigned int)rgmii_rx_rst,
    (unsigned int)rgmii_tx_rst
  );
  return cli_cmd_phylink_show(rgmii_idx);
}

/** @brief Prints one phylink CPU-port MAC/admin snapshot. */
static app_status_t cli_cmd_phylink_cpu_show(uint8_t cpu_port)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_phylink_cpu_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if (cli_is_managed_port((uint32_t)cpu_port) == 0u) {
    return APP_EINVAL;
  }

  st = lan9645_phylink_get_cpu_cfg(core, cpu_port, &cfg);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "phylink cpu port=%u admin=%u speed=%s duplex=%s mac_rx=%u mac_tx=%u\r\n",
    (unsigned int)cfg.port,
    (unsigned int)cfg.admin_up,
    cli_port_speed_to_str(cfg.speed_mbps),
    (cfg.full_duplex != 0u) ? "full" : "half",
    (unsigned int)cfg.mac_rx_ena,
    (unsigned int)cfg.mac_tx_ena
  );
  return APP_OK;
}

/** @brief Applies one phylink CPU-port MAC/admin configuration and prints readback state. */
static app_status_t cli_cmd_phylink_cpu_set(
  uint8_t cpu_port,
  uint8_t admin_up,
  uint16_t speed_mbps,
  uint8_t full_duplex
)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if ((cli_is_managed_port((uint32_t)cpu_port) == 0u) ||
      (admin_up > 1u) ||
      (full_duplex > 1u) ||
      ((speed_mbps != LAN9645_CORE_PORT_SPEED_10_MBPS) &&
       (speed_mbps != LAN9645_CORE_PORT_SPEED_100_MBPS))) {
    return APP_EINVAL;
  }

  st = lan9645_phylink_set_cpu_cfg(core, cpu_port, admin_up, speed_mbps, full_duplex);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "phylink cpu set port=%u admin=%u speed=%s duplex=%s\r\n",
    (unsigned int)cpu_port,
    (unsigned int)admin_up,
    cli_port_speed_to_str(speed_mbps),
    (full_duplex != 0u) ? "full" : "half"
  );
  return cli_cmd_phylink_cpu_show(cpu_port);
}

/** @brief Prints one NPI runtime configuration snapshot. */
static app_status_t cli_cmd_npi_show(void)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_npi_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }

  st = lan9645_npi_get_cfg(core, &cfg);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "npi port=%u ext_cpuq_mask=0x%02X ext_cpu_kill=%u int_cpu_kill=%u incl_xtr_hdr=%u incl_inj_hdr=%u rew_tag_cfg=%u src_pgid_mask=0x%03X\r\n",
    (unsigned int)cfg.npi_port,
    (unsigned int)cfg.ext_cpuq_mask,
    (unsigned int)cfg.ext_cpu_kill,
    (unsigned int)cfg.int_cpu_kill,
    (unsigned int)cfg.incl_xtr_hdr,
    (unsigned int)cfg.incl_inj_hdr,
    (unsigned int)cfg.rew_tag_cfg,
    (unsigned int)cfg.src_pgid_mask
  );
  return APP_OK;
}

/** @brief Updates NPI external CPU routing fields and prints readback state. */
static app_status_t cli_cmd_npi_ext_set(
  uint8_t npi_port,
  uint8_t ext_cpuq_mask,
  uint8_t ext_cpu_kill,
  uint8_t int_cpu_kill
)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_npi_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if ((npi_port > LAN9645_NPI_PORT_MAX) ||
      (ext_cpuq_mask > LAN9645_NPI_CPUQ_MASK_MAX) ||
      (ext_cpu_kill > 1u) ||
      (int_cpu_kill > 1u)) {
    return APP_EINVAL;
  }

  st = lan9645_npi_get_cfg(core, &cfg);
  if (st != APP_OK) {
    return st;
  }

  cfg.npi_port = npi_port;
  cfg.ext_cpuq_mask = ext_cpuq_mask;
  cfg.ext_cpu_kill = ext_cpu_kill;
  cfg.int_cpu_kill = int_cpu_kill;

  st = lan9645_npi_set_cfg(core, &cfg);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "npi ext set port=%u ext_cpuq_mask=0x%02X ext_cpu_kill=%u int_cpu_kill=%u\r\n",
    (unsigned int)npi_port,
    (unsigned int)ext_cpuq_mask,
    (unsigned int)ext_cpu_kill,
    (unsigned int)int_cpu_kill
  );
  return cli_cmd_npi_show();
}

/** @brief Updates NPI IFH prefix policy and prints readback state. */
static app_status_t cli_cmd_npi_ifh_set(uint8_t incl_xtr_hdr, uint8_t incl_inj_hdr)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_npi_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if ((incl_xtr_hdr > LAN9645_NPI_IFH_PREFIX_MAX) || (incl_inj_hdr > LAN9645_NPI_IFH_PREFIX_MAX)) {
    return APP_EINVAL;
  }

  st = lan9645_npi_get_cfg(core, &cfg);
  if (st != APP_OK) {
    return st;
  }

  cfg.incl_xtr_hdr = incl_xtr_hdr;
  cfg.incl_inj_hdr = incl_inj_hdr;

  st = lan9645_npi_set_cfg(core, &cfg);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "npi ifh set incl_xtr_hdr=%u incl_inj_hdr=%u\r\n",
    (unsigned int)incl_xtr_hdr,
    (unsigned int)incl_inj_hdr
  );
  return cli_cmd_npi_show();
}

/** @brief Updates NPI tag rewrite mode and prints readback state. */
static app_status_t cli_cmd_npi_rew_set(uint8_t rew_tag_cfg)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_npi_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if (rew_tag_cfg > LAN9645_NPI_REW_TAG_CFG_MAX) {
    return APP_EINVAL;
  }

  st = lan9645_npi_get_cfg(core, &cfg);
  if (st != APP_OK) {
    return st;
  }

  cfg.rew_tag_cfg = rew_tag_cfg;

  st = lan9645_npi_set_cfg(core, &cfg);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef("npi rew set tag_cfg=%u\r\n", (unsigned int)rew_tag_cfg);
  return cli_cmd_npi_show();
}

/** @brief Updates NPI source PGID destination mask and prints readback state. */
static app_status_t cli_cmd_npi_srcpgid_set(uint16_t src_pgid_mask)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_npi_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if (src_pgid_mask > LAN9645_NPI_PGID_MASK_MAX) {
    return APP_EINVAL;
  }

  st = lan9645_npi_get_cfg(core, &cfg);
  if (st != APP_OK) {
    return st;
  }

  cfg.src_pgid_mask = src_pgid_mask;

  st = lan9645_npi_set_cfg(core, &cfg);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef("npi srcpgid set mask=0x%03X\r\n", (unsigned int)src_pgid_mask);
  return cli_cmd_npi_show();
}

/** @brief Prints HSR/PRP runtime PGID state. */
static app_status_t cli_cmd_hsr_prp_show(void)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_hsr_prp_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }

  st = lan9645_hsr_prp_get_cfg(core, &cfg);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef("hsr_prp pgid_cpu_mask=0x%03X\r\n", (unsigned int)cfg.pgid_cpu_mask);
  return APP_OK;
}

/** @brief Writes HSR/PRP runtime PGID state and prints readback snapshot. */
static app_status_t cli_cmd_hsr_prp_set(uint16_t pgid_cpu_mask)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_hsr_prp_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if (pgid_cpu_mask > LAN9645_HSR_PRP_PGID_MASK_MAX) {
    return APP_EINVAL;
  }

  cfg.pgid_cpu_mask = pgid_cpu_mask;
  st = lan9645_hsr_prp_set_cfg(core, &cfg);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef("hsr_prp set pgid_cpu_mask=0x%03X\r\n", (unsigned int)pgid_cpu_mask);
  return cli_cmd_hsr_prp_show();
}

/** @brief Prints LAG hash runtime state. */
static app_status_t cli_cmd_lag_show(void)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_lag_cfg_t cfg = {0};
  uint32_t aggr_mask = 0u;
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }

  st = lan9645_lag_get_cfg(core, &cfg);
  if (st != APP_OK) {
    return st;
  }

  aggr_mask |= (cfg.ac_rnd_ena != 0u) ? LAN9645_ANA_AGGR_CFG_AC_RND_ENA : 0u;
  aggr_mask |= (cfg.ac_dmac_ena != 0u) ? LAN9645_ANA_AGGR_CFG_AC_DMAC_ENA : 0u;
  aggr_mask |= (cfg.ac_smac_ena != 0u) ? LAN9645_ANA_AGGR_CFG_AC_SMAC_ENA : 0u;
  aggr_mask |= (cfg.ac_ip6_flow_lbl_ena != 0u) ? LAN9645_ANA_AGGR_CFG_AC_IP6_FLOW_LBL_ENA : 0u;
  aggr_mask |= (cfg.ac_ip6_tcpudp_ena != 0u) ? LAN9645_ANA_AGGR_CFG_AC_IP6_TCPUDP_ENA : 0u;
  aggr_mask |= (cfg.ac_ip4_sipdip_ena != 0u) ? LAN9645_ANA_AGGR_CFG_AC_IP4_SIPDIP_ENA : 0u;
  aggr_mask |= (cfg.ac_ip4_tcpudp_ena != 0u) ? LAN9645_ANA_AGGR_CFG_AC_IP4_TCPUDP_ENA : 0u;

  (void)cli_writef(
    "lag aggr_mask=0x%02X rnd=%u dmac=%u smac=%u ip6_flow_lbl=%u ip6_tcpudp=%u ip4_sipdip=%u ip4_tcpudp=%u\r\n",
    (unsigned int)aggr_mask,
    (unsigned int)cfg.ac_rnd_ena,
    (unsigned int)cfg.ac_dmac_ena,
    (unsigned int)cfg.ac_smac_ena,
    (unsigned int)cfg.ac_ip6_flow_lbl_ena,
    (unsigned int)cfg.ac_ip6_tcpudp_ena,
    (unsigned int)cfg.ac_ip4_sipdip_ena,
    (unsigned int)cfg.ac_ip4_tcpudp_ena
  );
  return APP_OK;
}

/** @brief Writes LAG hash runtime state from one aggregation bit-mask value. */
static app_status_t cli_cmd_lag_set_mask(uint32_t aggr_mask)
{
  const uint32_t supported_mask =
    LAN9645_ANA_AGGR_CFG_AC_RND_ENA |
    LAN9645_ANA_AGGR_CFG_AC_DMAC_ENA |
    LAN9645_ANA_AGGR_CFG_AC_SMAC_ENA |
    LAN9645_ANA_AGGR_CFG_AC_IP6_FLOW_LBL_ENA |
    LAN9645_ANA_AGGR_CFG_AC_IP6_TCPUDP_ENA |
    LAN9645_ANA_AGGR_CFG_AC_IP4_SIPDIP_ENA |
    LAN9645_ANA_AGGR_CFG_AC_IP4_TCPUDP_ENA;
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_lag_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if ((aggr_mask & (~supported_mask)) != 0u) {
    return APP_EINVAL;
  }

  cfg.ac_rnd_ena = ((aggr_mask & LAN9645_ANA_AGGR_CFG_AC_RND_ENA) != 0u) ? 1u : 0u;
  cfg.ac_dmac_ena = ((aggr_mask & LAN9645_ANA_AGGR_CFG_AC_DMAC_ENA) != 0u) ? 1u : 0u;
  cfg.ac_smac_ena = ((aggr_mask & LAN9645_ANA_AGGR_CFG_AC_SMAC_ENA) != 0u) ? 1u : 0u;
  cfg.ac_ip6_flow_lbl_ena = ((aggr_mask & LAN9645_ANA_AGGR_CFG_AC_IP6_FLOW_LBL_ENA) != 0u) ? 1u : 0u;
  cfg.ac_ip6_tcpudp_ena = ((aggr_mask & LAN9645_ANA_AGGR_CFG_AC_IP6_TCPUDP_ENA) != 0u) ? 1u : 0u;
  cfg.ac_ip4_sipdip_ena = ((aggr_mask & LAN9645_ANA_AGGR_CFG_AC_IP4_SIPDIP_ENA) != 0u) ? 1u : 0u;
  cfg.ac_ip4_tcpudp_ena = ((aggr_mask & LAN9645_ANA_AGGR_CFG_AC_IP4_TCPUDP_ENA) != 0u) ? 1u : 0u;

  st = lan9645_lag_set_cfg(core, &cfg);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef("lag set aggr_mask=0x%02X\r\n", (unsigned int)aggr_mask);
  return cli_cmd_lag_show();
}

/** @brief Prints one MDB `ANA_PGID_CFG` entry snapshot. */
static app_status_t cli_cmd_mdb_cfg_show(uint8_t pgid_index)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_mdb_pgid_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if (pgid_index > LAN9645_MDB_PGID_INDEX_MAX) {
    return APP_EINVAL;
  }

  st = lan9645_mdb_get_pgid_cfg(core, pgid_index, &cfg);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "mdb cfg pgid=%u san=%u cpuq_dst=%u obey_vlan=%u\r\n",
    (unsigned int)cfg.pgid_index,
    (unsigned int)cfg.san_ena,
    (unsigned int)cfg.cpuq_dst_pgid,
    (unsigned int)cfg.obey_vlan
  );
  return APP_OK;
}

/** @brief Writes one MDB `ANA_PGID_CFG` entry and prints readback snapshot. */
static app_status_t cli_cmd_mdb_cfg_set(
  uint8_t pgid_index,
  uint8_t san_ena,
  uint8_t cpuq_dst_pgid,
  uint8_t obey_vlan
)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if ((pgid_index > LAN9645_MDB_PGID_INDEX_MAX) ||
      (san_ena > 1u) ||
      (cpuq_dst_pgid > LAN9645_MDB_CPUQ_DST_MAX) ||
      (obey_vlan > 1u)) {
    return APP_EINVAL;
  }

  st = lan9645_mdb_set_pgid_cfg(core, pgid_index, san_ena, cpuq_dst_pgid, obey_vlan);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "mdb cfg set pgid=%u san=%u cpuq_dst=%u obey_vlan=%u\r\n",
    (unsigned int)pgid_index,
    (unsigned int)san_ena,
    (unsigned int)cpuq_dst_pgid,
    (unsigned int)obey_vlan
  );
  return cli_cmd_mdb_cfg_show(pgid_index);
}

/** @brief Prints one MDB destination mask from `ANA_PGID`. */
static app_status_t cli_cmd_mdb_mask_show(uint8_t pgid_index)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  uint16_t member_mask = 0u;
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if (pgid_index > LAN9645_MDB_PGID_INDEX_MAX) {
    return APP_EINVAL;
  }

  st = lan9645_mdb_get_pgid_mask(core, pgid_index, &member_mask);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "mdb mask pgid=%u member_mask=0x%03X\r\n",
    (unsigned int)pgid_index,
    (unsigned int)member_mask
  );
  return APP_OK;
}

/** @brief Writes one MDB destination mask and prints readback snapshot. */
static app_status_t cli_cmd_mdb_mask_set(uint8_t pgid_index, uint16_t member_mask)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if ((pgid_index > LAN9645_MDB_PGID_INDEX_MAX) ||
      (member_mask > LAN9645_MDB_MEMBER_MASK_MAX)) {
    return APP_EINVAL;
  }

  st = lan9645_mdb_set_pgid_mask(core, pgid_index, member_mask);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "mdb mask set pgid=%u member_mask=0x%03X\r\n",
    (unsigned int)pgid_index,
    (unsigned int)member_mask
  );
  return cli_cmd_mdb_mask_show(pgid_index);
}

/** @brief Prints MDB multicast destination masks. */
static app_status_t cli_cmd_mdb_ipmc_show(void)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_mdb_ipmc_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }

  st = lan9645_mdb_get_ipmc_cfg(core, &cfg);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "mdb ipmc mcipv4_mask=0x%03X mcipv6_mask=0x%03X\r\n",
    (unsigned int)cfg.mcipv4_mask,
    (unsigned int)cfg.mcipv6_mask
  );
  return APP_OK;
}

/** @brief Writes MDB multicast destination masks and prints readback snapshot. */
static app_status_t cli_cmd_mdb_ipmc_set(uint16_t mcipv4_mask, uint16_t mcipv6_mask)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if ((mcipv4_mask > LAN9645_MDB_MEMBER_MASK_MAX) ||
      (mcipv6_mask > LAN9645_MDB_MEMBER_MASK_MAX)) {
    return APP_EINVAL;
  }

  st = lan9645_mdb_set_ipmc_cfg(core, mcipv4_mask, mcipv6_mask);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "mdb ipmc set mcipv4_mask=0x%03X mcipv6_mask=0x%03X\r\n",
    (unsigned int)mcipv4_mask,
    (unsigned int)mcipv6_mask
  );
  return cli_cmd_mdb_ipmc_show();
}

/** @brief Prints MRP runtime PGID state. */
static app_status_t cli_cmd_mrp_show(void)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_mrp_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }

  st = lan9645_mrp_get_cfg(core, &cfg);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef("mrp pgid_mrp_mask=0x%03X\r\n", (unsigned int)cfg.pgid_mrp_mask);
  return APP_OK;
}

/** @brief Writes MRP runtime PGID state and prints readback snapshot. */
static app_status_t cli_cmd_mrp_set(uint16_t pgid_mrp_mask)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_mrp_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if (pgid_mrp_mask > LAN9645_MRP_MEMBER_MASK_MAX) {
    return APP_EINVAL;
  }

  cfg.pgid_mrp_mask = pgid_mrp_mask;
  st = lan9645_mrp_set_cfg(core, &cfg);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef("mrp set pgid_mrp_mask=0x%03X\r\n", (unsigned int)pgid_mrp_mask);
  return cli_cmd_mrp_show();
}

/** @brief Prints PTP runtime state for global enable and managed wait-for-timestamp policy. */
static app_status_t cli_cmd_ptp_show(void)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_ptp_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }

  st = lan9645_ptp_get_cfg(core, &cfg);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "ptp global=%u wait_p0=%u wait_p1=%u wait_p8=%u\r\n",
    (unsigned int)cfg.ptp_enable,
    (unsigned int)cfg.wait_for_ts_port0,
    (unsigned int)cfg.wait_for_ts_port1,
    (unsigned int)cfg.wait_for_ts_port8
  );
  return APP_OK;
}

/** @brief Writes PTP global enable state and prints readback snapshot. */
static app_status_t cli_cmd_ptp_set_global(uint8_t enable)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_ptp_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if (enable > LAN9645_PTP_BOOL_MAX) {
    return APP_EINVAL;
  }

  st = lan9645_ptp_get_cfg(core, &cfg);
  if (st != APP_OK) {
    return st;
  }

  cfg.ptp_enable = enable;
  st = lan9645_ptp_set_cfg(core, &cfg);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef("ptp global=%u\r\n", (unsigned int)enable);
  return cli_cmd_ptp_show();
}

/** @brief Writes one managed-port wait-for-timestamp state and prints readback snapshot. */
static app_status_t cli_cmd_ptp_set_wait(uint8_t port, uint8_t wait_for_ts)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_ptp_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if ((cli_is_managed_port((uint32_t)port) == 0u) || (wait_for_ts > LAN9645_PTP_BOOL_MAX)) {
    return APP_EINVAL;
  }

  st = lan9645_ptp_get_cfg(core, &cfg);
  if (st != APP_OK) {
    return st;
  }

  if (port == 0u) {
    cfg.wait_for_ts_port0 = wait_for_ts;
  } else if (port == 1u) {
    cfg.wait_for_ts_port1 = wait_for_ts;
  } else {
    cfg.wait_for_ts_port8 = wait_for_ts;
  }

  st = lan9645_ptp_set_cfg(core, &cfg);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef("ptp wait port=%u wait_for_ts=%u\r\n", (unsigned int)port, (unsigned int)wait_for_ts);
  return cli_cmd_ptp_show();
}

/** @brief Prints one managed-port VCAP runtime configuration snapshot. */
static app_status_t cli_cmd_vcap_show(uint8_t port)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_vcap_port_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if (cli_is_managed_port((uint32_t)port) == 0u) {
    return APP_EINVAL;
  }

  st = lan9645_vcap_get_port_cfg(core, port, &cfg);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "vcap port=%u s1_raw=0x%08lX s2_raw=0x%08lX s1_ena=%u s2_ena=%u pag=%u ip6_cfg=%u\r\n",
    (unsigned int)cfg.port,
    (unsigned long)cfg.s1_raw,
    (unsigned long)cfg.s2_raw,
    (unsigned int)cfg.s1_ena,
    (unsigned int)cfg.s2_ena,
    (unsigned int)cfg.s1_pag_val,
    (unsigned int)cfg.s2_ip6_cfg
  );
  return APP_OK;
}

/** @brief Applies one managed-port VCAP configuration from raw S1/S2 register values. */
static app_status_t cli_cmd_vcap_set_raw(uint8_t port, uint32_t s1_raw, uint32_t s2_raw)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_vcap_port_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if (cli_is_managed_port((uint32_t)port) == 0u) {
    return APP_EINVAL;
  }

  cfg.port = port;
  cfg.s1_smac_ena = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_VCAP_CFG_S1_SMAC_ENA, s1_raw);
  cfg.s1_ena = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_VCAP_CFG_S1_ENA, s1_raw);
  cfg.s1_dmac_dip_ena = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_VCAP_CFG_S1_DMAC_DIP_ENA, s1_raw);
  cfg.s1_vlan_inner_tag_ena = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_VCAP_CFG_S1_VLAN_INNER_TAG_ENA, s1_raw);
  cfg.s1_pag_val = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_VCAP_CFG_PAG_VAL, s1_raw);
  cfg.s2_isdx_ena = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_VCAP_S2_CFG_ISDX_ENA, s2_raw);
  cfg.s2_udp_payload_ena = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_VCAP_S2_CFG_UDP_PAYLOAD_ENA, s2_raw);
  cfg.s2_etype_payload_ena = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_VCAP_S2_CFG_ETYPE_PAYLOAD_ENA, s2_raw);
  cfg.s2_ena = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_VCAP_S2_CFG_ENA, s2_raw);
  cfg.s2_snap_dis = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_VCAP_S2_CFG_SNAP_DIS, s2_raw);
  cfg.s2_arp_dis = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_VCAP_S2_CFG_ARP_DIS, s2_raw);
  cfg.s2_ip_tcpudp_dis = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_VCAP_S2_CFG_IP_TCPUDP_DIS, s2_raw);
  cfg.s2_ip_other_dis = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_VCAP_S2_CFG_IP_OTHER_DIS, s2_raw);
  cfg.s2_ip6_cfg = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_VCAP_S2_CFG_IP6_CFG, s2_raw);
  cfg.s2_oam_dis = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_VCAP_S2_CFG_OAM_DIS, s2_raw);

  st = lan9645_vcap_set_port_cfg(core, &cfg);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "vcap set port=%u s1_raw=0x%08lX s2_raw=0x%08lX\r\n",
    (unsigned int)port,
    (unsigned long)s1_raw,
    (unsigned long)s2_raw
  );
  return cli_cmd_vcap_show(port);
}

/** @brief Prints one managed-port tc-flower runtime state snapshot. */
static app_status_t cli_cmd_tcflower_show(uint8_t port)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_tc_flower_state_t state = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if (cli_is_managed_port((uint32_t)port) == 0u) {
    return APP_EINVAL;
  }

  st = lan9645_tc_flower_get_state(core, port, &state);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "tcflower port=%u enable=%u s1_ena=%u s1_smac=%u s1_dmac_dip=%u s1_inner=%u pag=%u\r\n",
    (unsigned int)state.port,
    (unsigned int)state.enable,
    (unsigned int)state.s1_ena,
    (unsigned int)state.s1_smac_ena,
    (unsigned int)state.s1_dmac_dip_ena,
    (unsigned int)state.s1_vlan_inner_tag_ena,
    (unsigned int)state.s1_pag_val
  );
  return APP_OK;
}

/** @brief Enables or disables tc-flower state on one managed port and prints readback. */
static app_status_t cli_cmd_tcflower_set(uint8_t port, uint8_t enable)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if ((cli_is_managed_port((uint32_t)port) == 0u) || (enable > 1u)) {
    return APP_EINVAL;
  }

  st = lan9645_tc_flower_set_enable(core, port, enable);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef("tcflower set port=%u enable=%u\r\n", (unsigned int)port, (unsigned int)enable);
  return cli_cmd_tcflower_show(port);
}

/** @brief Prints one managed-port tc-matchall runtime state snapshot. */
static app_status_t cli_cmd_tcmatch_show(uint8_t port)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_tc_matchall_state_t state = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if (cli_is_managed_port((uint32_t)port) == 0u) {
    return APP_EINVAL;
  }

  st = lan9645_tc_matchall_get_state(core, port, &state);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "tcmatch port=%u enable=%u s2_ena=%u isdx=%u udp=%u etype=%u ip6_cfg=%u\r\n",
    (unsigned int)state.port,
    (unsigned int)state.enable,
    (unsigned int)state.s2_ena,
    (unsigned int)state.s2_isdx_ena,
    (unsigned int)state.s2_udp_payload_ena,
    (unsigned int)state.s2_etype_payload_ena,
    (unsigned int)state.s2_ip6_cfg
  );
  return APP_OK;
}

/** @brief Enables or disables tc-matchall state on one managed port and prints readback. */
static app_status_t cli_cmd_tcmatch_set(uint8_t port, uint8_t enable)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if ((cli_is_managed_port((uint32_t)port) == 0u) || (enable > 1u)) {
    return APP_EINVAL;
  }

  st = lan9645_tc_matchall_set_enable(core, port, enable);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef("tcmatch set port=%u enable=%u\r\n", (unsigned int)port, (unsigned int)enable);
  return cli_cmd_tcmatch_show(port);
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

/** @brief Reads baseline SYS counters for one stat view and prints a compact snapshot. */
static app_status_t cli_cmd_cnt_read(uint16_t view)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_stats_sample_t sample = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }

  st = lan9645_stats_read(core, view, &sample);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "cnt view=%u rx_oct=%lu tx_oct=%lu dr_local=%lu sw_paused=%u\r\n",
    (unsigned int)sample.view,
    (unsigned long)sample.rx_oct,
    (unsigned long)sample.tx_oct,
    (unsigned long)sample.dr_local,
    (unsigned int)sample.sw_paused
  );
  return APP_OK;
}

/** @brief Triggers baseline port-counter clear-shot for one stat view. */
static app_status_t cli_cmd_cnt_clear(uint16_t view)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }

  st = lan9645_stats_clear(core, view);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "cnt clear view=%u sel=0x%02X\r\n",
    (unsigned int)view,
    (unsigned int)LAN9645_STATS_CLEAR_SEL_PORTS
  );
  return cli_cmd_cnt_read(view);
}

/** @brief Prints the Linux-reference feature matrix state and support coverage. */
static app_status_t cli_cmd_feat_list(void)
{
  lan9645_feature_matrix_t *matrix = lan9645_runtime_get_feature_matrix();
  lan9645_feature_summary_t summary = {0};
  app_status_t st = APP_OK;

  if (matrix == NULL) {
    return APP_ESTATE;
  }

  st = lan9645_feature_matrix_get_summary(matrix, &summary);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "feat summary total=%lu enabled=%lu backed=%lu configured=%lu failed=%lu\r\n",
    (unsigned long)summary.total_count,
    (unsigned long)summary.enabled_count,
    (unsigned long)summary.register_backed_count,
    (unsigned long)summary.configured_count,
    (unsigned long)summary.failed_count
  );

  for (uint32_t i = 0u; i < (uint32_t)LAN9645_FEATURE_COUNT; ++i) {
    lan9645_feature_state_t state = {0};
    st = lan9645_feature_matrix_get_state(matrix, (lan9645_feature_id_t)i, &state);
    if (st != APP_OK) {
      return st;
    }

    (void)cli_writef(
      "feat name=%s enabled=%u backed=%u configured=%u last=%s(%ld) apply=%lu fail=%lu\r\n",
      lan9645_feature_matrix_name((lan9645_feature_id_t)i),
      (unsigned int)state.enabled,
      (unsigned int)state.register_backed,
      (unsigned int)state.configured,
      cli_status_to_code(state.last_status),
      (long)state.last_status,
      (unsigned long)state.apply_count,
      (unsigned long)state.fail_count
    );
  }

  return APP_OK;
}

/** @brief Prints one feature state snapshot from the Linux-reference matrix. */
static app_status_t cli_cmd_feat_show(const char *name)
{
  lan9645_feature_matrix_t *matrix = lan9645_runtime_get_feature_matrix();
  lan9645_feature_state_t state = {0};
  lan9645_feature_id_t feature = LAN9645_FEATURE_AFI;
  app_status_t st = APP_OK;

  if ((matrix == NULL) || (name == NULL)) {
    return APP_ESTATE;
  }

  st = lan9645_feature_matrix_parse(name, &feature);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_feature_matrix_get_state(matrix, feature, &state);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "feat name=%s enabled=%u backed=%u configured=%u last=%s(%ld) apply=%lu fail=%lu\r\n",
    lan9645_feature_matrix_name(feature),
    (unsigned int)state.enabled,
    (unsigned int)state.register_backed,
    (unsigned int)state.configured,
    cli_status_to_code(state.last_status),
    (long)state.last_status,
    (unsigned long)state.apply_count,
    (unsigned long)state.fail_count
  );

  return APP_OK;
}

/** @brief Prints only the feature matrix aggregate summary. */
static app_status_t cli_cmd_feat_summary(void)
{
  lan9645_feature_matrix_t *matrix = lan9645_runtime_get_feature_matrix();
  lan9645_feature_summary_t summary = {0};
  app_status_t st = APP_OK;

  if (matrix == NULL) {
    return APP_ESTATE;
  }

  st = lan9645_feature_matrix_get_summary(matrix, &summary);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "feat summary total=%lu enabled=%lu backed=%lu configured=%lu failed=%lu\r\n",
    (unsigned long)summary.total_count,
    (unsigned long)summary.enabled_count,
    (unsigned long)summary.register_backed_count,
    (unsigned long)summary.configured_count,
    (unsigned long)summary.failed_count
  );

  return APP_OK;
}

/** @brief Enables or disables one Linux-reference feature in the runtime profile. */
static app_status_t cli_cmd_feat_enable(const char *name, uint8_t enable)
{
  lan9645_feature_matrix_t *matrix = lan9645_runtime_get_feature_matrix();
  lan9645_feature_id_t feature = LAN9645_FEATURE_AFI;
  app_status_t st = APP_OK;

  if ((matrix == NULL) || (name == NULL)) {
    return APP_ESTATE;
  }

  st = lan9645_feature_matrix_parse(name, &feature);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_feature_matrix_set_enable(matrix, feature, enable);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "feat enable name=%s state=%s\r\n",
    lan9645_feature_matrix_name(feature),
    (enable != 0u) ? "on" : "off"
  );
  return APP_OK;
}

/** @brief Applies one feature or all enabled features from the Linux-reference matrix. */
static app_status_t cli_cmd_feat_apply(const char *name)
{
  lan9645_feature_matrix_t *matrix = lan9645_runtime_get_feature_matrix();
  app_status_t st = APP_OK;

  if ((matrix == NULL) || (name == NULL)) {
    return APP_ESTATE;
  }

  if (cli_eq(name, "all") != 0u) {
    st = lan9645_feature_matrix_apply_all(matrix);
    (void)cli_writef("feat apply target=all st=%s(%ld)\r\n", cli_status_to_code(st), (long)st);
    return st;
  }

  {
    lan9645_feature_id_t feature = LAN9645_FEATURE_AFI;
    st = lan9645_feature_matrix_parse(name, &feature);
    if (st != APP_OK) {
      return st;
    }

    st = lan9645_feature_matrix_apply(matrix, feature);
    (void)cli_writef(
      "feat apply target=%s st=%s(%ld)\r\n",
      lan9645_feature_matrix_name(feature),
      cli_status_to_code(st),
      (long)st
    );
  }

  return st;
}

/** @brief Prints AFI runtime enable state. */
static app_status_t cli_cmd_afi_show(void)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_afi_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }

  st = lan9645_afi_get_cfg(core, &cfg);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef("afi enable=%u\r\n", (unsigned int)cfg.afi_enable);
  return APP_OK;
}

/** @brief Writes AFI runtime enable state and prints readback snapshot. */
static app_status_t cli_cmd_afi_set(uint8_t enable)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_afi_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if (enable > LAN9645_AFI_BOOL_MAX) {
    return APP_EINVAL;
  }

  cfg.afi_enable = enable;
  st = lan9645_afi_set_cfg(core, &cfg);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef("afi set enable=%u\r\n", (unsigned int)enable);
  return cli_cmd_afi_show();
}

/** @brief Prints manual-injection header policy for one port. */
static app_status_t cli_cmd_inj_show(uint8_t port)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_manual_inj_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if (port > LAN9645_MANUAL_INJ_PORT_MAX) {
    return APP_EINVAL;
  }

  st = lan9645_manual_inj_get_cfg(core, port, &cfg);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "inj port=%u incl_inj_hdr=%u\r\n",
    (unsigned int)cfg.port,
    (unsigned int)cfg.incl_inj_hdr
  );
  return APP_OK;
}

/** @brief Writes manual-injection header policy and prints readback snapshot. */
static app_status_t cli_cmd_inj_set(uint8_t port, uint8_t incl_inj_hdr)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  lan9645_manual_inj_cfg_t cfg = {0};
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if ((port > LAN9645_MANUAL_INJ_PORT_MAX) ||
      (incl_inj_hdr > LAN9645_MANUAL_INJ_INCL_INJ_HDR_MAX)) {
    return APP_EINVAL;
  }

  cfg.port = port;
  cfg.incl_inj_hdr = incl_inj_hdr;
  st = lan9645_manual_inj_set_cfg(core, &cfg);
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef(
    "inj set port=%u incl_inj_hdr=%u\r\n",
    (unsigned int)port,
    (unsigned int)incl_inj_hdr
  );
  return cli_cmd_inj_show(port);
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

  {
    lan9645_feature_matrix_t *matrix = lan9645_runtime_get_feature_matrix();
    if (matrix != NULL) {
      lan9645_feature_summary_t feat_summary = {0};
      if (lan9645_feature_matrix_get_summary(matrix, &feat_summary) == APP_OK) {
        (void)cli_writef(
          "feat summary total=%lu enabled=%lu backed=%lu configured=%lu failed=%lu\r\n",
          (unsigned long)feat_summary.total_count,
          (unsigned long)feat_summary.enabled_count,
          (unsigned long)feat_summary.register_backed_count,
          (unsigned long)feat_summary.configured_count,
          (unsigned long)feat_summary.failed_count
        );
      }
    }
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
  lan9645_core_irq_analysis_t irq_eval = {0};
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
      st = lan9645_core_eval_irq_actions(&irq_regs, &irq_eval);
      if (st == APP_OK) {
        (void)cli_writef(
          "irq eval cause=%u sev=%s ack=%u dev_pending=0x%03lX drop=0x%08lX warn=0x%08lX err=0x%08lX actions=0x%02lX\r\n",
          (unsigned int)irq_eval.has_cause,
          cli_irq_severity_to_str(irq_eval.severity_code),
          (unsigned int)irq_eval.ack_required,
          (unsigned long)irq_eval.dev_pending,
          (unsigned long)irq_eval.drop_events,
          (unsigned long)irq_eval.warn_events,
          (unsigned long)irq_eval.err_events,
          (unsigned long)irq_eval.action_mask
        );
      } else {
        (void)cli_writef("irq eval unavailable st=%ld\r\n", (long)st);
      }
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
  uint32_t remaining = 0u;
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }

  st = lan9645_core_get_irq_regs(core, &irq_regs);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_core_ack_irq_checked(core, irq_regs.ana_anevents, &remaining);
  if (st != APP_OK) {
    (void)cli_writef(
      "irq ack failed ana_anevents=0x%08lX remaining=0x%08lX st=%ld\r\n",
      (unsigned long)irq_regs.ana_anevents,
      (unsigned long)remaining,
      (long)st
    );
    return st;
  }

  (void)cli_writef(
    "irq ack ana_anevents=0x%08lX remaining=0x%08lX\r\n",
    (unsigned long)irq_regs.ana_anevents,
    (unsigned long)remaining
  );
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
  if (cli_cmd_feat_summary() != APP_OK) {
    pass = 0u;
  } else {
    lan9645_feature_matrix_t *matrix = lan9645_runtime_get_feature_matrix();
    lan9645_feature_summary_t summary = {0};
    if ((matrix == NULL) || (lan9645_feature_matrix_get_summary(matrix, &summary) != APP_OK)) {
      pass = 0u;
      (void)cli_writef("bringup feat result=FAIL reason=matrix_unavailable\r\n");
    } else {
      uint8_t feat_pass = 1u;
      if (summary.failed_count != 0u) {
        feat_pass = 0u;
      }
      if (summary.configured_count < summary.enabled_count) {
        feat_pass = 0u;
      }
      (void)cli_writef(
        "bringup feat enabled=%lu configured=%lu failed=%lu result=%s\r\n",
        (unsigned long)summary.enabled_count,
        (unsigned long)summary.configured_count,
        (unsigned long)summary.failed_count,
        (feat_pass != 0u) ? "PASS" : "FAIL"
      );
      if (feat_pass == 0u) {
        pass = 0u;
      }
    }
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

  if (cli_eq(tokens[0], "cli") != 0u) {
    if ((ntok < 2u) || (cli_eq(tokens[1], "contract") == 0u)) {
      return APP_EINVAL;
    }
    return cli_cmd_contract_show();
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

  if (cli_eq(tokens[0], "feat") != 0u) {
    if (ntok < 2u) {
      return APP_EINVAL;
    }

    if (cli_eq(tokens[1], "list") != 0u) {
      return cli_cmd_feat_list();
    }
    if (cli_eq(tokens[1], "summary") != 0u) {
      return cli_cmd_feat_summary();
    }
    if (cli_eq(tokens[1], "show") != 0u) {
      if (ntok < 3u) {
        return APP_EINVAL;
      }
      return cli_cmd_feat_show(tokens[2]);
    }
    if (cli_eq(tokens[1], "enable") != 0u) {
      uint8_t enable = 0u;
      if (ntok < 4u) {
        return APP_EINVAL;
      }
      if (cli_eq(tokens[3], "on") != 0u) {
        enable = 1u;
      } else if (cli_eq(tokens[3], "off") != 0u) {
        enable = 0u;
      } else {
        return APP_EINVAL;
      }
      return cli_cmd_feat_enable(tokens[2], enable);
    }
    if (cli_eq(tokens[1], "apply") != 0u) {
      if (ntok < 3u) {
        return APP_EINVAL;
      }
      return cli_cmd_feat_apply(tokens[2]);
    }
    return APP_EINVAL;
  }

  if (cli_eq(tokens[0], "afi") != 0u) {
    if (ntok < 2u) {
      return APP_EINVAL;
    }
    if (cli_eq(tokens[1], "show") != 0u) {
      return cli_cmd_afi_show();
    }
    if (cli_eq(tokens[1], "set") != 0u) {
      uint32_t enable = 0u;
      if (ntok < 3u) {
        return APP_EINVAL;
      }
      if ((cli_parse_u32(tokens[2], &enable) != APP_OK) || (enable > LAN9645_AFI_BOOL_MAX)) {
        return APP_EINVAL;
      }
      return cli_cmd_afi_set((uint8_t)enable);
    }
    return APP_EINVAL;
  }

  if (cli_eq(tokens[0], "inj") != 0u) {
    uint32_t port = 0u;
    if (ntok < 3u) {
      return APP_EINVAL;
    }
    if ((cli_parse_u32(tokens[2], &port) != APP_OK) ||
        (port > LAN9645_MANUAL_INJ_PORT_MAX)) {
      return APP_EINVAL;
    }

    if (cli_eq(tokens[1], "show") != 0u) {
      return cli_cmd_inj_show((uint8_t)port);
    }
    if (cli_eq(tokens[1], "set") != 0u) {
      uint32_t incl_inj_hdr = 0u;
      if (ntok < 4u) {
        return APP_EINVAL;
      }
      if ((cli_parse_u32(tokens[3], &incl_inj_hdr) != APP_OK) ||
          (incl_inj_hdr > LAN9645_MANUAL_INJ_INCL_INJ_HDR_MAX)) {
        return APP_EINVAL;
      }
      return cli_cmd_inj_set((uint8_t)port, (uint8_t)incl_inj_hdr);
    }
    return APP_EINVAL;
  }

  if (cli_eq(tokens[0], "eee") != 0u) {
    uint32_t port = 0u;
    if (ntok < 3u) {
      return APP_EINVAL;
    }
    if (cli_parse_u32(tokens[2], &port) != APP_OK) {
      return APP_EINVAL;
    }
    if (cli_is_managed_port(port) == 0u) {
      return APP_EINVAL;
    }

    if (cli_eq(tokens[1], "show") != 0u) {
      return cli_cmd_eee_show((uint8_t)port);
    }
    if (cli_eq(tokens[1], "set") != 0u) {
      uint32_t ena = 0u;
      uint32_t wakeup = 0u;
      uint32_t holdoff = 0u;
      if (ntok < 6u) {
        return APP_EINVAL;
      }
      if ((cli_parse_u32(tokens[3], &ena) != APP_OK) ||
          (cli_parse_u32(tokens[4], &wakeup) != APP_OK) ||
          (cli_parse_u32(tokens[5], &holdoff) != APP_OK)) {
        return APP_EINVAL;
      }
      if ((ena > 1u) ||
          (wakeup > LAN9645_EEE_WAKEUP_TIMER_MAX) ||
          (holdoff > LAN9645_EEE_HOLDOFF_TIMER_MAX)) {
        return APP_EINVAL;
      }
      return cli_cmd_eee_set((uint8_t)port, (uint8_t)ena, (uint8_t)wakeup, (uint8_t)holdoff);
    }
    return APP_EINVAL;
  }

  if (cli_eq(tokens[0], "fp") != 0u) {
    if (ntok < 2u) {
      return APP_EINVAL;
    }

    if (cli_eq(tokens[1], "show") != 0u) {
      uint32_t port = 0u;
      if (ntok < 3u) {
        return APP_EINVAL;
      }
      if (cli_parse_u32(tokens[2], &port) != APP_OK) {
        return APP_EINVAL;
      }
      if (cli_is_managed_port(port) == 0u) {
        return APP_EINVAL;
      }
      return cli_cmd_fp_show((uint8_t)port);
    }

    if (cli_eq(tokens[1], "set") != 0u) {
      uint32_t port = 0u;
      uint32_t mmrx = 0u;
      uint32_t mmtx = 0u;
      uint32_t keep = 0u;
      uint32_t pqueues = 0u;
      uint32_t strict_ipg = 0u;
      if (ntok < 8u) {
        return APP_EINVAL;
      }
      if ((cli_parse_u32(tokens[2], &port) != APP_OK) ||
          (cli_parse_u32(tokens[3], &mmrx) != APP_OK) ||
          (cli_parse_u32(tokens[4], &mmtx) != APP_OK) ||
          (cli_parse_u32(tokens[5], &keep) != APP_OK) ||
          (cli_parse_u32(tokens[6], &pqueues) != APP_OK) ||
          (cli_parse_u32(tokens[7], &strict_ipg) != APP_OK)) {
        return APP_EINVAL;
      }
      if ((cli_is_managed_port(port) == 0u) ||
          (mmrx > 1u) ||
          (mmtx > 1u) ||
          (keep > 1u) ||
          (pqueues > LAN9645_FP_PREEMPT_QUEUE_MASK_MAX) ||
          (strict_ipg > LAN9645_FP_STRICT_IPG_MAX)) {
        return APP_EINVAL;
      }
      return cli_cmd_fp_set(
        (uint8_t)port,
        (uint8_t)mmrx,
        (uint8_t)mmtx,
        (uint8_t)keep,
        (uint8_t)pqueues,
        (uint8_t)strict_ipg
      );
    }

    if (cli_eq(tokens[1], "verify") != 0u) {
      uint32_t port = 0u;
      if (ntok < 4u) {
        return APP_EINVAL;
      }
      if (cli_parse_u32(tokens[3], &port) != APP_OK) {
        return APP_EINVAL;
      }
      if (cli_is_managed_port(port) == 0u) {
        return APP_EINVAL;
      }

      if (cli_eq(tokens[2], "show") != 0u) {
        return cli_cmd_fp_show((uint8_t)port);
      }
      if (cli_eq(tokens[2], "set") != 0u) {
        uint32_t dis = 0u;
        uint32_t time = 0u;
        uint32_t units = 0u;
        if (ntok < 7u) {
          return APP_EINVAL;
        }
        if ((cli_parse_u32(tokens[4], &dis) != APP_OK) ||
            (cli_parse_u32(tokens[5], &time) != APP_OK) ||
            (cli_parse_u32(tokens[6], &units) != APP_OK)) {
          return APP_EINVAL;
        }
        if ((dis > 1u) ||
            (time > LAN9645_FP_VERIFY_TIME_MAX) ||
            (units > LAN9645_FP_VERIFY_UNITS_MAX)) {
          return APP_EINVAL;
        }
        return cli_cmd_fp_verify_set((uint8_t)port, (uint8_t)dis, (uint8_t)time, (uint8_t)units);
      }

      return APP_EINVAL;
    }

    if (cli_eq(tokens[1], "frag") != 0u) {
      uint32_t port = 0u;
      if (ntok < 4u) {
        return APP_EINVAL;
      }
      if (cli_parse_u32(tokens[3], &port) != APP_OK) {
        return APP_EINVAL;
      }
      if (cli_is_managed_port(port) == 0u) {
        return APP_EINVAL;
      }

      if (cli_eq(tokens[2], "show") != 0u) {
        return cli_cmd_fp_show((uint8_t)port);
      }
      if (cli_eq(tokens[2], "set") != 0u) {
        uint32_t size = 0u;
        if (ntok < 5u) {
          return APP_EINVAL;
        }
        if (cli_parse_u32(tokens[4], &size) != APP_OK) {
          return APP_EINVAL;
        }
        if (size > LAN9645_FP_ADD_FRAG_SIZE_MAX) {
          return APP_EINVAL;
        }
        return cli_cmd_fp_frag_set((uint8_t)port, (uint8_t)size);
      }

      return APP_EINVAL;
    }

    return APP_EINVAL;
  }

  if (cli_eq(tokens[0], "dcb") != 0u) {
    uint32_t port = 0u;
    if (ntok < 3u) {
      return APP_EINVAL;
    }
    if (cli_parse_u32(tokens[2], &port) != APP_OK) {
      return APP_EINVAL;
    }
    if (cli_is_managed_port(port) == 0u) {
      return APP_EINVAL;
    }

    if (cli_eq(tokens[1], "show") != 0u) {
      return cli_cmd_dcb_show((uint8_t)port);
    }
    if (cli_eq(tokens[1], "trust") != 0u) {
      uint32_t trust_pcp = 0u;
      uint32_t trust_dscp = 0u;
      if (ntok < 5u) {
        return APP_EINVAL;
      }
      if ((cli_parse_u32(tokens[3], &trust_pcp) != APP_OK) ||
          (cli_parse_u32(tokens[4], &trust_dscp) != APP_OK)) {
        return APP_EINVAL;
      }
      if ((trust_pcp > 1u) || (trust_dscp > 1u)) {
        return APP_EINVAL;
      }
      return cli_cmd_dcb_trust_set((uint8_t)port, (uint8_t)trust_pcp, (uint8_t)trust_dscp);
    }
    if (cli_eq(tokens[1], "default") != 0u) {
      uint32_t default_prio = 0u;
      uint32_t default_dp = 0u;
      if (ntok < 5u) {
        return APP_EINVAL;
      }
      if ((cli_parse_u32(tokens[3], &default_prio) != APP_OK) ||
          (cli_parse_u32(tokens[4], &default_dp) != APP_OK)) {
        return APP_EINVAL;
      }
      if ((default_prio > LAN9645_DCB_DEFAULT_PRIO_MAX) ||
          (default_dp > LAN9645_DCB_DEFAULT_DP_MAX)) {
        return APP_EINVAL;
      }
      return cli_cmd_dcb_default_set((uint8_t)port, (uint8_t)default_prio, (uint8_t)default_dp);
    }
    if (cli_eq(tokens[1], "pfc") != 0u) {
      uint32_t vlan_pfc_enable = 0u;
      uint32_t rx_pfc_mask = 0u;
      uint32_t tx_pfc_mask = 0u;
      if (ntok < 6u) {
        return APP_EINVAL;
      }
      if ((cli_parse_u32(tokens[3], &vlan_pfc_enable) != APP_OK) ||
          (cli_parse_u32(tokens[4], &rx_pfc_mask) != APP_OK) ||
          (cli_parse_u32(tokens[5], &tx_pfc_mask) != APP_OK)) {
        return APP_EINVAL;
      }
      if ((vlan_pfc_enable > 1u) ||
          (rx_pfc_mask > LAN9645_DCB_PFC_MASK_MAX) ||
          (tx_pfc_mask > LAN9645_DCB_PFC_MASK_MAX)) {
        return APP_EINVAL;
      }
      return cli_cmd_dcb_pfc_set(
        (uint8_t)port,
        (uint8_t)vlan_pfc_enable,
        (uint8_t)rx_pfc_mask,
        (uint8_t)tx_pfc_mask
      );
    }
    return APP_EINVAL;
  }

  if (cli_eq(tokens[0], "mac") != 0u) {
    uint32_t port = 0u;
    if (ntok < 3u) {
      return APP_EINVAL;
    }
    if (cli_parse_u32(tokens[2], &port) != APP_OK) {
      return APP_EINVAL;
    }
    if (cli_is_managed_port(port) == 0u) {
      return APP_EINVAL;
    }

    if (cli_eq(tokens[1], "show") != 0u) {
      return cli_cmd_mac_show((uint8_t)port);
    }
    if (cli_eq(tokens[1], "set") != 0u) {
      uint8_t admin_up = 0u;
      uint8_t full_duplex = 0u;
      uint32_t speed_mbps = 0u;
      if (ntok < 6u) {
        return APP_EINVAL;
      }

      if (cli_eq(tokens[3], "up") != 0u) {
        admin_up = 1u;
      } else if (cli_eq(tokens[3], "down") != 0u) {
        admin_up = 0u;
      } else {
        return APP_EINVAL;
      }

      if (cli_parse_u32(tokens[4], &speed_mbps) != APP_OK) {
        return APP_EINVAL;
      }
      if ((speed_mbps != LAN9645_CORE_PORT_SPEED_10_MBPS) &&
          (speed_mbps != LAN9645_CORE_PORT_SPEED_100_MBPS)) {
        return APP_EINVAL;
      }

      if (cli_eq(tokens[5], "full") != 0u) {
        full_duplex = 1u;
      } else if (cli_eq(tokens[5], "half") != 0u) {
        full_duplex = 0u;
      } else {
        return APP_EINVAL;
      }

      return cli_cmd_mac_set((uint8_t)port, admin_up, (uint16_t)speed_mbps, full_duplex);
    }

    return APP_EINVAL;
  }

  if (cli_eq(tokens[0], "mirror") != 0u) {
    if (ntok < 2u) {
      return APP_EINVAL;
    }

    if (cli_eq(tokens[1], "show") != 0u) {
      return cli_cmd_mirror_show();
    }
    if (cli_eq(tokens[1], "set") != 0u) {
      uint32_t ingress_mask = 0u;
      uint32_t egress_mask = 0u;
      if (ntok < 4u) {
        return APP_EINVAL;
      }
      if ((cli_parse_u32(tokens[2], &ingress_mask) != APP_OK) ||
          (cli_parse_u32(tokens[3], &egress_mask) != APP_OK)) {
        return APP_EINVAL;
      }
      if ((ingress_mask > LAN9645_MIRROR_PORT_MASK_MAX) ||
          (egress_mask > LAN9645_MIRROR_PORT_MASK_MAX)) {
        return APP_EINVAL;
      }
      return cli_cmd_mirror_set((uint16_t)ingress_mask, (uint16_t)egress_mask);
    }

    if (cli_eq(tokens[1], "src") != 0u) {
      uint32_t port = 0u;
      if (ntok < 4u) {
        return APP_EINVAL;
      }
      if (cli_parse_u32(tokens[3], &port) != APP_OK) {
        return APP_EINVAL;
      }
      if (cli_is_managed_port(port) == 0u) {
        return APP_EINVAL;
      }
      if (cli_eq(tokens[2], "show") != 0u) {
        return cli_cmd_mirror_src_show((uint8_t)port);
      }
      if (cli_eq(tokens[2], "set") != 0u) {
        uint32_t enable = 0u;
        if (ntok < 5u) {
          return APP_EINVAL;
        }
        if (cli_parse_u32(tokens[4], &enable) != APP_OK) {
          return APP_EINVAL;
        }
        if (enable > 1u) {
          return APP_EINVAL;
        }
        return cli_cmd_mirror_src_set((uint8_t)port, (uint8_t)enable);
      }
      return APP_EINVAL;
    }

    return APP_EINVAL;
  }

  if (cli_eq(tokens[0], "bum") != 0u) {
    if (ntok < 2u) {
      return APP_EINVAL;
    }

    if (cli_eq(tokens[1], "flood") != 0u) {
      uint32_t priority = 0u;
      if (ntok < 4u) {
        return APP_EINVAL;
      }
      if (cli_parse_u32(tokens[3], &priority) != APP_OK) {
        return APP_EINVAL;
      }
      if (priority >= LAN9645_BUM_PRIO_COUNT) {
        return APP_EINVAL;
      }
      if (cli_eq(tokens[2], "show") != 0u) {
        return cli_cmd_bum_flood_show((uint8_t)priority);
      }
      if (cli_eq(tokens[2], "set") != 0u) {
        uint32_t pgid_uc = 0u;
        uint32_t pgid_bc = 0u;
        uint32_t pgid_mc = 0u;
        if (ntok < 7u) {
          return APP_EINVAL;
        }
        if ((cli_parse_u32(tokens[4], &pgid_uc) != APP_OK) ||
            (cli_parse_u32(tokens[5], &pgid_bc) != APP_OK) ||
            (cli_parse_u32(tokens[6], &pgid_mc) != APP_OK)) {
          return APP_EINVAL;
        }
        if ((pgid_uc > LAN9645_BUM_PGID_MAX) ||
            (pgid_bc > LAN9645_BUM_PGID_MAX) ||
            (pgid_mc > LAN9645_BUM_PGID_MAX)) {
          return APP_EINVAL;
        }
        return cli_cmd_bum_flood_set(
          (uint8_t)priority,
          (uint8_t)pgid_uc,
          (uint8_t)pgid_bc,
          (uint8_t)pgid_mc
        );
      }
      return APP_EINVAL;
    }

    if (cli_eq(tokens[1], "ipmc") != 0u) {
      if (ntok < 3u) {
        return APP_EINVAL;
      }
      if (cli_eq(tokens[2], "show") != 0u) {
        return cli_cmd_bum_ipmc_show();
      }
      if (cli_eq(tokens[2], "set") != 0u) {
        uint32_t pgid_mc4_ctrl = 0u;
        uint32_t pgid_mc4_data = 0u;
        uint32_t pgid_mc6_ctrl = 0u;
        uint32_t pgid_mc6_data = 0u;
        if (ntok < 7u) {
          return APP_EINVAL;
        }
        if ((cli_parse_u32(tokens[3], &pgid_mc4_ctrl) != APP_OK) ||
            (cli_parse_u32(tokens[4], &pgid_mc4_data) != APP_OK) ||
            (cli_parse_u32(tokens[5], &pgid_mc6_ctrl) != APP_OK) ||
            (cli_parse_u32(tokens[6], &pgid_mc6_data) != APP_OK)) {
          return APP_EINVAL;
        }
        if ((pgid_mc4_ctrl > LAN9645_BUM_PGID_MAX) ||
            (pgid_mc4_data > LAN9645_BUM_PGID_MAX) ||
            (pgid_mc6_ctrl > LAN9645_BUM_PGID_MAX) ||
            (pgid_mc6_data > LAN9645_BUM_PGID_MAX)) {
          return APP_EINVAL;
        }
        return cli_cmd_bum_ipmc_set(
          (uint8_t)pgid_mc4_ctrl,
          (uint8_t)pgid_mc4_data,
          (uint8_t)pgid_mc6_ctrl,
          (uint8_t)pgid_mc6_data
        );
      }
      return APP_EINVAL;
    }

    if (cli_eq(tokens[1], "pgid") != 0u) {
      uint32_t pgid = 0u;
      if (ntok < 4u) {
        return APP_EINVAL;
      }
      if (cli_parse_u32(tokens[3], &pgid) != APP_OK) {
        return APP_EINVAL;
      }
      if (pgid > LAN9645_BUM_PGID_MAX) {
        return APP_EINVAL;
      }
      if (cli_eq(tokens[2], "show") != 0u) {
        return cli_cmd_bum_pgid_show((uint8_t)pgid);
      }
      if (cli_eq(tokens[2], "set") != 0u) {
        uint32_t member_mask = 0u;
        if (ntok < 5u) {
          return APP_EINVAL;
        }
        if (cli_parse_u32(tokens[4], &member_mask) != APP_OK) {
          return APP_EINVAL;
        }
        if (member_mask > LAN9645_BUM_MEMBER_MASK_MAX) {
          return APP_EINVAL;
        }
        return cli_cmd_bum_pgid_set((uint8_t)pgid, (uint16_t)member_mask);
      }
      return APP_EINVAL;
    }

    return APP_EINVAL;
  }

  if (cli_eq(tokens[0], "cut") != 0u) {
    uint32_t p = 0u;
    if (ntok < 3u) {
      return APP_EINVAL;
    }
    if (cli_parse_u32(tokens[2], &p) != APP_OK) {
      return APP_EINVAL;
    }
    if (cli_is_managed_port(p) == 0u) {
      return APP_EINVAL;
    }

    if (cli_eq(tokens[1], "show") != 0u) {
      return cli_cmd_cut_show((uint8_t)p);
    }
    if (cli_eq(tokens[1], "set") != 0u) {
      uint32_t spd = 0u;
      uint32_t mask = 0u;
      if (ntok < 5u) {
        return APP_EINVAL;
      }
      if ((cli_parse_u32(tokens[3], &spd) != APP_OK) ||
          (cli_parse_u32(tokens[4], &mask) != APP_OK)) {
        return APP_EINVAL;
      }
      if ((spd > LAN9645_CUT_THRU_SPD_MAX) || (mask > LAN9645_CUT_THRU_ENA_MASK_MAX)) {
        return APP_EINVAL;
      }
      return cli_cmd_cut_set((uint8_t)p, (uint8_t)spd, (uint8_t)mask);
    }

    return APP_EINVAL;
  }

  if (cli_eq(tokens[0], "tbf") != 0u) {
    uint32_t p = 0u;
    if (ntok < 3u) {
      return APP_EINVAL;
    }
    if (cli_parse_u32(tokens[2], &p) != APP_OK) {
      return APP_EINVAL;
    }
    if (cli_is_managed_port(p) == 0u) {
      return APP_EINVAL;
    }

    if (cli_eq(tokens[1], "pshow") != 0u) {
      return cli_cmd_tbf_show((uint8_t)p, LAN9645_TBF_SCOPE_PORT, 0u);
    }
    if (cli_eq(tokens[1], "qshow") != 0u) {
      uint32_t q = 0u;
      if (ntok < 4u) {
        return APP_EINVAL;
      }
      if (cli_parse_u32(tokens[3], &q) != APP_OK) {
        return APP_EINVAL;
      }
      if (q >= LAN9645_TBF_QUEUE_COUNT) {
        return APP_EINVAL;
      }
      return cli_cmd_tbf_show((uint8_t)p, LAN9645_TBF_SCOPE_QUEUE, (uint8_t)q);
    }
    if (cli_eq(tokens[1], "pset") != 0u) {
      uint32_t avb = 0u;
      uint32_t frm = 0u;
      uint32_t rate = 0u;
      uint32_t burst = 0u;
      if (ntok < 7u) {
        return APP_EINVAL;
      }
      if ((cli_parse_u32(tokens[3], &avb) != APP_OK) ||
          (cli_parse_u32(tokens[4], &frm) != APP_OK) ||
          (cli_parse_u32(tokens[5], &rate) != APP_OK) ||
          (cli_parse_u32(tokens[6], &burst) != APP_OK)) {
        return APP_EINVAL;
      }
      if ((avb > 1u) ||
          (frm > LAN9645_TBF_FRM_MODE_MAX) ||
          (rate > LAN9645_TBF_CIR_RATE_MAX) ||
          (burst > LAN9645_TBF_CIR_BURST_MAX)) {
        return APP_EINVAL;
      }
      return cli_cmd_tbf_set(
        (uint8_t)p,
        LAN9645_TBF_SCOPE_PORT,
        0u,
        (uint8_t)avb,
        (uint8_t)frm,
        (uint16_t)rate,
        (uint8_t)burst
      );
    }
    if (cli_eq(tokens[1], "qset") != 0u) {
      uint32_t q = 0u;
      uint32_t avb = 0u;
      uint32_t frm = 0u;
      uint32_t rate = 0u;
      uint32_t burst = 0u;
      if (ntok < 8u) {
        return APP_EINVAL;
      }
      if ((cli_parse_u32(tokens[3], &q) != APP_OK) ||
          (cli_parse_u32(tokens[4], &avb) != APP_OK) ||
          (cli_parse_u32(tokens[5], &frm) != APP_OK) ||
          (cli_parse_u32(tokens[6], &rate) != APP_OK) ||
          (cli_parse_u32(tokens[7], &burst) != APP_OK)) {
        return APP_EINVAL;
      }
      if ((q >= LAN9645_TBF_QUEUE_COUNT) ||
          (avb > 1u) ||
          (frm > LAN9645_TBF_FRM_MODE_MAX) ||
          (rate > LAN9645_TBF_CIR_RATE_MAX) ||
          (burst > LAN9645_TBF_CIR_BURST_MAX)) {
        return APP_EINVAL;
      }
      return cli_cmd_tbf_set(
        (uint8_t)p,
        LAN9645_TBF_SCOPE_QUEUE,
        (uint8_t)q,
        (uint8_t)avb,
        (uint8_t)frm,
        (uint16_t)rate,
        (uint8_t)burst
      );
    }
    return APP_EINVAL;
  }

  if (cli_eq(tokens[0], "cbs") != 0u) {
    uint32_t p = 0u;
    uint32_t q = 0u;
    if (ntok < 4u) {
      return APP_EINVAL;
    }
    if ((cli_parse_u32(tokens[2], &p) != APP_OK) ||
        (cli_parse_u32(tokens[3], &q) != APP_OK)) {
      return APP_EINVAL;
    }
    if ((cli_is_managed_port(p) == 0u) || (q >= LAN9645_CBS_QUEUE_COUNT)) {
      return APP_EINVAL;
    }

    if (cli_eq(tokens[1], "qshow") != 0u) {
      return cli_cmd_cbs_show((uint8_t)p, (uint8_t)q);
    }
    if (cli_eq(tokens[1], "qset") != 0u) {
      uint32_t avb = 0u;
      uint32_t frm = 0u;
      uint32_t rate = 0u;
      uint32_t burst = 0u;
      if (ntok < 8u) {
        return APP_EINVAL;
      }
      if ((cli_parse_u32(tokens[4], &avb) != APP_OK) ||
          (cli_parse_u32(tokens[5], &frm) != APP_OK) ||
          (cli_parse_u32(tokens[6], &rate) != APP_OK) ||
          (cli_parse_u32(tokens[7], &burst) != APP_OK)) {
        return APP_EINVAL;
      }
      if ((avb > 1u) ||
          (frm > LAN9645_CBS_FRM_MODE_MAX) ||
          (rate > LAN9645_CBS_CIR_RATE_MAX) ||
          (burst > LAN9645_CBS_CIR_BURST_MAX)) {
        return APP_EINVAL;
      }
      return cli_cmd_cbs_set(
        (uint8_t)p,
        (uint8_t)q,
        (uint8_t)avb,
        (uint8_t)frm,
        (uint16_t)rate,
        (uint8_t)burst
      );
    }
    return APP_EINVAL;
  }

  if (cli_eq(tokens[0], "taprio") != 0u) {
    if (ntok < 2u) {
      return APP_EINVAL;
    }

    if (cli_eq(tokens[1], "show") != 0u) {
      return cli_cmd_taprio_show();
    }
    if (cli_eq(tokens[1], "set") != 0u) {
      uint32_t revisit_dly = 0u;
      uint32_t list_num_max = 0u;
      uint32_t guard = 0u;
      if (ntok < 5u) {
        return APP_EINVAL;
      }
      if ((cli_parse_u32(tokens[2], &revisit_dly) != APP_OK) ||
          (cli_parse_u32(tokens[3], &list_num_max) != APP_OK) ||
          (cli_parse_u32(tokens[4], &guard) != APP_OK)) {
        return APP_EINVAL;
      }
      if ((revisit_dly > LAN9645_TAPRIO_REVISIT_DLY_MAX) ||
          (list_num_max > LAN9645_TAPRIO_LIST_NUM_MAX_MAX) ||
          (guard > 1u)) {
        return APP_EINVAL;
      }
      return cli_cmd_taprio_set((uint8_t)revisit_dly, (uint8_t)list_num_max, (uint8_t)guard);
    }

    if (cli_eq(tokens[1], "gate") != 0u) {
      uint32_t port = 0u;
      if (ntok < 4u) {
        return APP_EINVAL;
      }
      if (cli_parse_u32(tokens[3], &port) != APP_OK) {
        return APP_EINVAL;
      }
      if (cli_is_managed_port(port) == 0u) {
        return APP_EINVAL;
      }
      if (cli_eq(tokens[2], "show") != 0u) {
        return cli_cmd_taprio_gate_show((uint8_t)port);
      }
      if (cli_eq(tokens[2], "set") != 0u) {
        uint32_t mask = 0u;
        if (ntok < 5u) {
          return APP_EINVAL;
        }
        if (cli_parse_u32(tokens[4], &mask) != APP_OK) {
          return APP_EINVAL;
        }
        if (mask > LAN9645_TAPRIO_GATE_MASK_MAX) {
          return APP_EINVAL;
        }
        return cli_cmd_taprio_gate_set((uint8_t)port, (uint8_t)mask);
      }
      return APP_EINVAL;
    }

    if (cli_eq(tokens[1], "list") != 0u) {
      uint32_t list_num = 0u;
      if (ntok < 4u) {
        return APP_EINVAL;
      }
      if (cli_parse_u32(tokens[3], &list_num) != APP_OK) {
        return APP_EINVAL;
      }
      if (list_num > LAN9645_TAPRIO_LIST_NUM_MAX) {
        return APP_EINVAL;
      }
      if (cli_eq(tokens[2], "show") != 0u) {
        return cli_cmd_taprio_list_show((uint8_t)list_num);
      }
      if (cli_eq(tokens[2], "set") != 0u) {
        uint32_t list_state = 0u;
        if (ntok < 5u) {
          return APP_EINVAL;
        }
        if (cli_parse_u32(tokens[4], &list_state) != APP_OK) {
          return APP_EINVAL;
        }
        if (list_state > LAN9645_TAPRIO_LIST_STATE_MAX) {
          return APP_EINVAL;
        }
        return cli_cmd_taprio_list_set((uint8_t)list_num, (uint8_t)list_state);
      }
      return APP_EINVAL;
    }

    return APP_EINVAL;
  }

  if (cli_eq(tokens[0], "police") != 0u) {
    if (ntok < 2u) {
      return APP_EINVAL;
    }

    if (cli_eq(tokens[1], "pshow") != 0u) {
      uint32_t port = 0u;
      if (ntok < 3u) {
        return APP_EINVAL;
      }
      if (cli_parse_u32(tokens[2], &port) != APP_OK) {
        return APP_EINVAL;
      }
      if (cli_is_managed_port(port) == 0u) {
        return APP_EINVAL;
      }
      return cli_cmd_police_port_show((uint8_t)port);
    }

    if (cli_eq(tokens[1], "pset") != 0u) {
      uint32_t port = 0u;
      uint32_t redir8021 = 0u;
      uint32_t redirip = 0u;
      uint32_t portena = 0u;
      uint32_t qmask = 0u;
      uint32_t order = 0u;
      if (ntok < 8u) {
        return APP_EINVAL;
      }
      if ((cli_parse_u32(tokens[2], &port) != APP_OK) ||
          (cli_parse_u32(tokens[3], &redir8021) != APP_OK) ||
          (cli_parse_u32(tokens[4], &redirip) != APP_OK) ||
          (cli_parse_u32(tokens[5], &portena) != APP_OK) ||
          (cli_parse_u32(tokens[6], &qmask) != APP_OK) ||
          (cli_parse_u32(tokens[7], &order) != APP_OK)) {
        return APP_EINVAL;
      }
      if ((cli_is_managed_port(port) == 0u) ||
          (redir8021 > 1u) ||
          (redirip > 1u) ||
          (portena > 1u) ||
          (qmask > LAN9645_POLICE_QUEUE_MASK_MAX) ||
          (order > LAN9645_POLICE_ORDER_MAX)) {
        return APP_EINVAL;
      }
      return cli_cmd_police_port_set(
        (uint8_t)port,
        (uint8_t)redir8021,
        (uint8_t)redirip,
        (uint8_t)portena,
        (uint8_t)qmask,
        (uint16_t)order
      );
    }

    if (cli_eq(tokens[1], "ishow") != 0u) {
      uint32_t pol_idx = 0u;
      if (ntok < 3u) {
        return APP_EINVAL;
      }
      if (cli_parse_u32(tokens[2], &pol_idx) != APP_OK) {
        return APP_EINVAL;
      }
      if (pol_idx >= LAN9645_POLICE_PORT_POL_COUNT) {
        return APP_EINVAL;
      }
      return cli_cmd_police_index_show((uint8_t)pol_idx);
    }

    if (cli_eq(tokens[1], "imode") != 0u) {
      uint32_t pol_idx = 0u;
      uint32_t drop = 0u;
      uint32_t mark_red = 0u;
      uint32_t frm = 0u;
      uint32_t cir = 0u;
      uint32_t over = 0u;
      if (ntok < 8u) {
        return APP_EINVAL;
      }
      if ((cli_parse_u32(tokens[2], &pol_idx) != APP_OK) ||
          (cli_parse_u32(tokens[3], &drop) != APP_OK) ||
          (cli_parse_u32(tokens[4], &mark_red) != APP_OK) ||
          (cli_parse_u32(tokens[5], &frm) != APP_OK) ||
          (cli_parse_u32(tokens[6], &cir) != APP_OK) ||
          (cli_parse_u32(tokens[7], &over) != APP_OK)) {
        return APP_EINVAL;
      }
      if ((pol_idx >= LAN9645_POLICE_PORT_POL_COUNT) ||
          (drop > 1u) ||
          (mark_red > 1u) ||
          (frm > LAN9645_POLICE_FRM_MODE_MAX) ||
          (cir > 1u) ||
          (over > 1u)) {
        return APP_EINVAL;
      }
      return cli_cmd_police_index_mode_set(
        (uint8_t)pol_idx,
        (uint8_t)drop,
        (uint8_t)mark_red,
        (uint8_t)frm,
        (uint8_t)cir,
        (uint8_t)over
      );
    }

    if (cli_eq(tokens[1], "iopt") != 0u) {
      uint32_t pol_idx = 0u;
      uint32_t ipg = 0u;
      uint32_t dlb = 0u;
      if (ntok < 5u) {
        return APP_EINVAL;
      }
      if ((cli_parse_u32(tokens[2], &pol_idx) != APP_OK) ||
          (cli_parse_u32(tokens[3], &ipg) != APP_OK) ||
          (cli_parse_u32(tokens[4], &dlb) != APP_OK)) {
        return APP_EINVAL;
      }
      if ((pol_idx >= LAN9645_POLICE_PORT_POL_COUNT) ||
          (ipg > LAN9645_POLICE_IPG_SIZE_MAX) ||
          (dlb > 1u)) {
        return APP_EINVAL;
      }
      return cli_cmd_police_index_opt_set((uint8_t)pol_idx, (uint8_t)ipg, (uint8_t)dlb);
    }

    if (cli_eq(tokens[1], "ipir") != 0u) {
      uint32_t pol_idx = 0u;
      uint32_t level = 0u;
      uint32_t rate = 0u;
      uint32_t burst = 0u;
      if (ntok < 6u) {
        return APP_EINVAL;
      }
      if ((cli_parse_u32(tokens[2], &pol_idx) != APP_OK) ||
          (cli_parse_u32(tokens[3], &level) != APP_OK) ||
          (cli_parse_u32(tokens[4], &rate) != APP_OK) ||
          (cli_parse_u32(tokens[5], &burst) != APP_OK)) {
        return APP_EINVAL;
      }
      if ((pol_idx >= LAN9645_POLICE_PORT_POL_COUNT) ||
          (level > LAN9645_POLICE_PIR_LVL_MAX) ||
          (rate > LAN9645_POLICE_PIR_RATE_MAX) ||
          (burst > LAN9645_POLICE_PIR_BURST_MAX)) {
        return APP_EINVAL;
      }
      return cli_cmd_police_index_pir_set(
        (uint8_t)pol_idx,
        level,
        (uint16_t)rate,
        (uint8_t)burst
      );
    }

    return APP_EINVAL;
  }

  if (cli_eq(tokens[0], "stream") != 0u) {
    uint32_t sfid_index = 0u;
    if (ntok < 3u) {
      return APP_EINVAL;
    }
    if (cli_parse_u32(tokens[2], &sfid_index) != APP_OK) {
      return APP_EINVAL;
    }
    if (sfid_index > LAN9645_STREAM_SFID_INDEX_MAX) {
      return APP_EINVAL;
    }

    if (cli_eq(tokens[1], "show") != 0u) {
      return cli_cmd_stream_show((uint8_t)sfid_index);
    }
    if (cli_eq(tokens[1], "set") != 0u) {
      uint32_t bo_frm = 0u;
      uint32_t bo_frm_enable = 0u;
      uint32_t force_block = 0u;
      uint32_t max_sdu_len = 0u;
      if (ntok < 7u) {
        return APP_EINVAL;
      }
      if ((cli_parse_u32(tokens[3], &bo_frm) != APP_OK) ||
          (cli_parse_u32(tokens[4], &bo_frm_enable) != APP_OK) ||
          (cli_parse_u32(tokens[5], &force_block) != APP_OK) ||
          (cli_parse_u32(tokens[6], &max_sdu_len) != APP_OK)) {
        return APP_EINVAL;
      }
      if ((bo_frm > 1u) ||
          (bo_frm_enable > 1u) ||
          (force_block > 1u) ||
          (max_sdu_len > LAN9645_STREAM_MAX_SDU_LEN_MAX)) {
        return APP_EINVAL;
      }
      return cli_cmd_stream_set(
        (uint8_t)sfid_index,
        (uint8_t)bo_frm,
        (uint8_t)bo_frm_enable,
        (uint8_t)force_block,
        (uint16_t)max_sdu_len
      );
    }

    return APP_EINVAL;
  }

  if (cli_eq(tokens[0], "psfp") != 0u) {
    if (ntok < 3u) {
      return APP_EINVAL;
    }

    if (cli_eq(tokens[1], "sfid") != 0u) {
      uint32_t sfid_index = 0u;
      if (ntok < 4u) {
        return APP_EINVAL;
      }
      if (cli_parse_u32(tokens[3], &sfid_index) != APP_OK) {
        return APP_EINVAL;
      }
      if (sfid_index > LAN9645_PSFP_INDEX_MAX) {
        return APP_EINVAL;
      }

      if (cli_eq(tokens[2], "show") != 0u) {
        return cli_cmd_psfp_sfid_show((uint8_t)sfid_index);
      }
      if (cli_eq(tokens[2], "set") != 0u) {
        uint32_t bo_frm = 0u;
        uint32_t bo_frm_enable = 0u;
        uint32_t force_block = 0u;
        uint32_t max_sdu_len = 0u;
        if (ntok < 8u) {
          return APP_EINVAL;
        }
        if ((cli_parse_u32(tokens[4], &bo_frm) != APP_OK) ||
            (cli_parse_u32(tokens[5], &bo_frm_enable) != APP_OK) ||
            (cli_parse_u32(tokens[6], &force_block) != APP_OK) ||
            (cli_parse_u32(tokens[7], &max_sdu_len) != APP_OK)) {
          return APP_EINVAL;
        }
        if ((bo_frm > 1u) ||
            (bo_frm_enable > 1u) ||
            (force_block > 1u) ||
            (max_sdu_len > LAN9645_PSFP_MAX_SDU_LEN_MAX)) {
          return APP_EINVAL;
        }
        return cli_cmd_psfp_sfid_set(
          (uint8_t)sfid_index,
          (uint8_t)bo_frm,
          (uint8_t)bo_frm_enable,
          (uint8_t)force_block,
          (uint16_t)max_sdu_len
        );
      }
      return APP_EINVAL;
    }

    if (cli_eq(tokens[1], "gate") != 0u) {
      uint32_t sgid = 0u;
      if (ntok < 4u) {
        return APP_EINVAL;
      }
      if (cli_parse_u32(tokens[3], &sgid) != APP_OK) {
        return APP_EINVAL;
      }
      if (sgid > LAN9645_PSFP_INDEX_MAX) {
        return APP_EINVAL;
      }

      if (cli_eq(tokens[2], "show") != 0u) {
        return cli_cmd_psfp_gate_show((uint8_t)sgid);
      }
      if (cli_eq(tokens[2], "set") != 0u) {
        uint32_t list_length = 0u;
        uint32_t gate_enable = 0u;
        uint32_t init_ips = 0u;
        uint32_t init_gate_state = 0u;
        if (ntok < 8u) {
          return APP_EINVAL;
        }
        if ((cli_parse_u32(tokens[4], &list_length) != APP_OK) ||
            (cli_parse_u32(tokens[5], &gate_enable) != APP_OK) ||
            (cli_parse_u32(tokens[6], &init_ips) != APP_OK) ||
            (cli_parse_u32(tokens[7], &init_gate_state) != APP_OK)) {
          return APP_EINVAL;
        }
        if ((list_length > LAN9645_PSFP_LIST_LENGTH_MAX) ||
            (gate_enable > 1u) ||
            (init_ips > LAN9645_PSFP_INIT_IPS_MAX) ||
            (init_gate_state > 1u)) {
          return APP_EINVAL;
        }
        return cli_cmd_psfp_gate_set(
          (uint8_t)sgid,
          (uint8_t)list_length,
          (uint8_t)gate_enable,
          (uint8_t)init_ips,
          (uint8_t)init_gate_state
        );
      }
      if (cli_eq(tokens[2], "commit") != 0u) {
        return cli_cmd_psfp_gate_commit((uint8_t)sgid);
      }
      return APP_EINVAL;
    }

    return APP_EINVAL;
  }

  if ((cli_eq(tokens[0], "mqprio") != 0u) || (cli_eq(tokens[0], "ets") != 0u)) {
    const char *domain = tokens[0];
    uint32_t p = 0u;
    if (ntok < 3u) {
      return APP_EINVAL;
    }
    if (cli_parse_u32(tokens[2], &p) != APP_OK) {
      return APP_EINVAL;
    }
    if (cli_is_managed_port(p) == 0u) {
      return APP_EINVAL;
    }

    if (cli_eq(tokens[1], "show") != 0u) {
      return cli_cmd_sched_show_domain(domain, (uint8_t)p);
    }

    if (cli_eq(tokens[1], "weight") != 0u) {
      uint32_t q = 0u;
      uint32_t cost = 0u;
      if (ntok < 5u) {
        return APP_EINVAL;
      }
      if ((cli_parse_u32(tokens[3], &q) != APP_OK) ||
          (cli_parse_u32(tokens[4], &cost) != APP_OK)) {
        return APP_EINVAL;
      }
      if ((q >= LAN9645_QOS_SCHED_QUEUE_COUNT) || (cost > LAN9645_QOS_DWRR_COST_MAX)) {
        return APP_EINVAL;
      }
      return cli_cmd_sched_weight_domain(domain, (uint8_t)p, (uint8_t)q, (uint8_t)cost);
    }

    if (cli_eq(tokens[1], "mode") != 0u) {
      lan9645_qos_sched_mode_t mode = LAN9645_QOS_SCHED_STRICT;
      if (ntok < 4u) {
        return APP_EINVAL;
      }
      if (cli_eq(tokens[3], "strict") != 0u) {
        mode = LAN9645_QOS_SCHED_STRICT;
      } else if (cli_eq(tokens[3], "dwrr") != 0u) {
        mode = LAN9645_QOS_SCHED_DWRR;
      } else {
        return APP_EINVAL;
      }
      return cli_cmd_sched_set_domain(domain, (uint8_t)p, mode);
    }

    return APP_EINVAL;
  }

  if (cli_eq(tokens[0], "qos") != 0u) {
    if (ntok < 2u) {
      return APP_EINVAL;
    }

    if (cli_eq(tokens[1], "dscp") != 0u) {
      uint32_t dscp = 0u;
      if (ntok < 4u) {
        return APP_EINVAL;
      }
      if (cli_eq(tokens[2], "show") != 0u) {
        if (cli_parse_u32(tokens[3], &dscp) != APP_OK) {
          return APP_EINVAL;
        }
        if (dscp > LAN9645_QOS_DSCP_MAX) {
          return APP_EINVAL;
        }
        return cli_cmd_qos_dscp_show((uint8_t)dscp);
      }
      if (cli_eq(tokens[2], "set") != 0u) {
        uint32_t prio = 0u;
        uint32_t dp = 0u;
        uint32_t trust = 0u;
        uint32_t rewrite = 0u;
        if (ntok < 8u) {
          return APP_EINVAL;
        }
        if ((cli_parse_u32(tokens[3], &dscp) != APP_OK) ||
            (cli_parse_u32(tokens[4], &prio) != APP_OK) ||
            (cli_parse_u32(tokens[5], &dp) != APP_OK) ||
            (cli_parse_u32(tokens[6], &trust) != APP_OK) ||
            (cli_parse_u32(tokens[7], &rewrite) != APP_OK)) {
          return APP_EINVAL;
        }
        if ((dscp > LAN9645_QOS_DSCP_MAX) ||
            (prio > LAN9645_QOS_DEFAULT_PRIO_MAX) ||
            (dp > LAN9645_QOS_DEFAULT_DP_MAX) ||
            (trust > 1u) ||
            (rewrite > 1u)) {
          return APP_EINVAL;
        }
        return cli_cmd_qos_dscp_set((uint8_t)dscp, (uint8_t)prio, (uint8_t)dp, (uint8_t)trust, (uint8_t)rewrite);
      }
      return APP_EINVAL;
    } else if (cli_eq(tokens[1], "pcpdei") != 0u) {
      uint32_t port = 0u;
      uint32_t pcp = 0u;
      uint32_t dei = 0u;
      if (ntok < 6u) {
        return APP_EINVAL;
      }
      if ((cli_parse_u32(tokens[3], &port) != APP_OK) ||
          (cli_parse_u32(tokens[4], &pcp) != APP_OK) ||
          (cli_parse_u32(tokens[5], &dei) != APP_OK)) {
        return APP_EINVAL;
      }
      if ((cli_is_managed_port(port) == 0u) ||
          (pcp > LAN9645_QOS_PCP_MAX) ||
          (dei > LAN9645_QOS_DEI_MAX)) {
        return APP_EINVAL;
      }
      if (cli_eq(tokens[2], "show") != 0u) {
        return cli_cmd_qos_pcpdei_show((uint8_t)port, (uint8_t)pcp, (uint8_t)dei);
      }
      if (cli_eq(tokens[2], "set") != 0u) {
        uint32_t qos_prio = 0u;
        uint32_t dp = 0u;
        uint32_t rew_pcp = 0u;
        uint32_t rew_dei = 0u;
        if (ntok < 10u) {
          return APP_EINVAL;
        }
        if ((cli_parse_u32(tokens[6], &qos_prio) != APP_OK) ||
            (cli_parse_u32(tokens[7], &dp) != APP_OK) ||
            (cli_parse_u32(tokens[8], &rew_pcp) != APP_OK) ||
            (cli_parse_u32(tokens[9], &rew_dei) != APP_OK)) {
          return APP_EINVAL;
        }
        if ((qos_prio > LAN9645_QOS_DEFAULT_PRIO_MAX) ||
            (dp > LAN9645_QOS_DEFAULT_DP_MAX) ||
            (rew_pcp > LAN9645_QOS_PCP_MAX) ||
            (rew_dei > LAN9645_QOS_DEI_MAX)) {
          return APP_EINVAL;
        }
        return cli_cmd_qos_pcpdei_set(
          (uint8_t)port,
          (uint8_t)pcp,
          (uint8_t)dei,
          (uint8_t)qos_prio,
          (uint8_t)dp,
          (uint8_t)rew_pcp,
          (uint8_t)rew_dei
        );
      }
      return APP_EINVAL;
    } else if (cli_eq(tokens[1], "rew") != 0u) {
      uint32_t port = 0u;
      if (ntok < 4u) {
        return APP_EINVAL;
      }
      if (cli_parse_u32(tokens[3], &port) != APP_OK) {
        return APP_EINVAL;
      }
      if (cli_is_managed_port(port) == 0u) {
        return APP_EINVAL;
      }
      if (cli_eq(tokens[2], "show") != 0u) {
        return cli_cmd_qos_rew_show((uint8_t)port);
      }
      if (cli_eq(tokens[2], "set") != 0u) {
        uint32_t tag_pcp_cfg = 0u;
        uint32_t tag_dei_cfg = 0u;
        if (ntok < 6u) {
          return APP_EINVAL;
        }
        if ((cli_parse_u32(tokens[4], &tag_pcp_cfg) != APP_OK) ||
            (cli_parse_u32(tokens[5], &tag_dei_cfg) != APP_OK)) {
          return APP_EINVAL;
        }
        if ((tag_pcp_cfg > LAN9645_QOS_REW_TAG_CFG_MAX) ||
            (tag_dei_cfg > LAN9645_QOS_REW_TAG_CFG_MAX)) {
          return APP_EINVAL;
        }
        return cli_cmd_qos_rew_set((uint8_t)port, (uint8_t)tag_pcp_cfg, (uint8_t)tag_dei_cfg);
      }
      return APP_EINVAL;
    } else if (cli_eq(tokens[1], "tagmode") != 0u) {
      uint32_t port = 0u;
      if (ntok < 4u) {
        return APP_EINVAL;
      }
      if (cli_parse_u32(tokens[3], &port) != APP_OK) {
        return APP_EINVAL;
      }
      if (cli_is_managed_port(port) == 0u) {
        return APP_EINVAL;
      }
      if (cli_eq(tokens[2], "show") != 0u) {
        return cli_cmd_qos_tagmode_show((uint8_t)port);
      }
      if (cli_eq(tokens[2], "set") != 0u) {
        uint32_t tag_cfg = 0u;
        if (ntok < 5u) {
          return APP_EINVAL;
        }
        if (cli_parse_u32(tokens[4], &tag_cfg) != APP_OK) {
          return APP_EINVAL;
        }
        if (tag_cfg > LAN9645_QOS_REW_TAG_MODE_MAX) {
          return APP_EINVAL;
        }
        return cli_cmd_qos_tagmode_set((uint8_t)port, (uint8_t)tag_cfg);
      }
      return APP_EINVAL;
    } else if (cli_eq(tokens[1], "rewport") != 0u) {
      uint32_t port = 0u;
      if (ntok < 4u) {
        return APP_EINVAL;
      }
      if (cli_parse_u32(tokens[3], &port) != APP_OK) {
        return APP_EINVAL;
      }
      if (cli_is_managed_port(port) == 0u) {
        return APP_EINVAL;
      }
      if (cli_eq(tokens[2], "show") != 0u) {
        return cli_cmd_qos_rewport_show((uint8_t)port);
      }
      if (cli_eq(tokens[2], "set") != 0u) {
        uint32_t port_pcp = 0u;
        uint32_t port_dei = 0u;
        if (ntok < 6u) {
          return APP_EINVAL;
        }
        if ((cli_parse_u32(tokens[4], &port_pcp) != APP_OK) ||
            (cli_parse_u32(tokens[5], &port_dei) != APP_OK)) {
          return APP_EINVAL;
        }
        if ((port_pcp > LAN9645_QOS_PCP_MAX) ||
            (port_dei > LAN9645_QOS_DEI_MAX)) {
          return APP_EINVAL;
        }
        return cli_cmd_qos_rewport_set((uint8_t)port, (uint8_t)port_pcp, (uint8_t)port_dei);
      }
      return APP_EINVAL;
    } else {
      uint32_t p = 0u;
      if (ntok < 3u) {
        return APP_EINVAL;
      }
      if (cli_parse_u32(tokens[2], &p) != APP_OK) {
        return APP_EINVAL;
      }
      if (cli_is_managed_port(p) == 0u) {
        return APP_EINVAL;
      }

      if (cli_eq(tokens[1], "show") != 0u) {
        return cli_cmd_qos_show((uint8_t)p);
      }

      if (cli_eq(tokens[1], "trust") != 0u) {
        uint8_t trust_pcp = 0u;
        uint8_t trust_dscp = 0u;
        if (ntok < 4u) {
          return APP_EINVAL;
        }
        if (cli_eq(tokens[3], "none") != 0u) {
          trust_pcp = 0u;
          trust_dscp = 0u;
        } else if (cli_eq(tokens[3], "pcp") != 0u) {
          trust_pcp = 1u;
          trust_dscp = 0u;
        } else if (cli_eq(tokens[3], "dscp") != 0u) {
          trust_pcp = 0u;
          trust_dscp = 1u;
        } else if (cli_eq(tokens[3], "both") != 0u) {
          trust_pcp = 1u;
          trust_dscp = 1u;
        } else {
          return APP_EINVAL;
        }
        return cli_cmd_qos_trust((uint8_t)p, trust_pcp, trust_dscp);
      }

      if (cli_eq(tokens[1], "default") != 0u) {
        uint32_t prio = 0u;
        uint32_t dp = 0u;
        if (ntok < 5u) {
          return APP_EINVAL;
        }
        if ((cli_parse_u32(tokens[3], &prio) != APP_OK) ||
            (cli_parse_u32(tokens[4], &dp) != APP_OK)) {
          return APP_EINVAL;
        }
        if ((prio > LAN9645_QOS_DEFAULT_PRIO_MAX) || (dp > LAN9645_QOS_DEFAULT_DP_MAX)) {
          return APP_EINVAL;
        }
        return cli_cmd_qos_default((uint8_t)p, (uint8_t)prio, (uint8_t)dp);
      }
    }

    return APP_EINVAL;
  }

  if (cli_eq(tokens[0], "phylink") != 0u) {
    if (ntok < 2u) {
      return APP_EINVAL;
    }

    if (cli_eq(tokens[1], "show") != 0u) {
      uint32_t rgmii_idx = 0u;
      if (ntok < 3u) {
        return APP_EINVAL;
      }
      if ((cli_parse_u32(tokens[2], &rgmii_idx) != APP_OK) ||
          (rgmii_idx > LAN9645_PHYLINK_RGMII_IDX_MAX)) {
        return APP_EINVAL;
      }
      return cli_cmd_phylink_show((uint8_t)rgmii_idx);
    }

    if (cli_eq(tokens[1], "set") != 0u) {
      uint32_t rgmii_idx = 0u;
      uint32_t ib_enable = 0u;
      uint32_t ib_rx_enable = 0u;
      uint32_t ib_tx_enable = 0u;
      uint32_t tx_clk_cfg = 0u;
      uint32_t rgmii_rx_rst = 0u;
      uint32_t rgmii_tx_rst = 0u;
      if (ntok < 9u) {
        return APP_EINVAL;
      }
      if ((cli_parse_u32(tokens[2], &rgmii_idx) != APP_OK) ||
          (cli_parse_u32(tokens[3], &ib_enable) != APP_OK) ||
          (cli_parse_u32(tokens[4], &ib_rx_enable) != APP_OK) ||
          (cli_parse_u32(tokens[5], &ib_tx_enable) != APP_OK) ||
          (cli_parse_u32(tokens[6], &tx_clk_cfg) != APP_OK) ||
          (cli_parse_u32(tokens[7], &rgmii_rx_rst) != APP_OK) ||
          (cli_parse_u32(tokens[8], &rgmii_tx_rst) != APP_OK)) {
        return APP_EINVAL;
      }
      if ((rgmii_idx > LAN9645_PHYLINK_RGMII_IDX_MAX) ||
          (ib_enable > 1u) ||
          (ib_rx_enable > 1u) ||
          (ib_tx_enable > 1u) ||
          (tx_clk_cfg > LAN9645_PHYLINK_TX_CLK_CFG_MAX) ||
          (rgmii_rx_rst > 1u) ||
          (rgmii_tx_rst > 1u)) {
        return APP_EINVAL;
      }
      return cli_cmd_phylink_set(
        (uint8_t)rgmii_idx,
        (uint8_t)ib_enable,
        (uint8_t)ib_rx_enable,
        (uint8_t)ib_tx_enable,
        (uint8_t)tx_clk_cfg,
        (uint8_t)rgmii_rx_rst,
        (uint8_t)rgmii_tx_rst
      );
    }

    if (cli_eq(tokens[1], "cpu") != 0u) {
      uint32_t cpu_port = 0u;
      if (ntok < 4u) {
        return APP_EINVAL;
      }
      if ((cli_parse_u32(tokens[3], &cpu_port) != APP_OK) ||
          (cli_is_managed_port(cpu_port) == 0u)) {
        return APP_EINVAL;
      }

      if (cli_eq(tokens[2], "show") != 0u) {
        return cli_cmd_phylink_cpu_show((uint8_t)cpu_port);
      }

      if (cli_eq(tokens[2], "set") != 0u) {
        uint32_t admin_up = 0u;
        uint32_t speed_mbps = 0u;
        uint32_t full_duplex = 0u;
        if (ntok < 7u) {
          return APP_EINVAL;
        }
        if ((cli_parse_u32(tokens[4], &admin_up) != APP_OK) ||
            (cli_parse_u32(tokens[5], &speed_mbps) != APP_OK) ||
            (cli_parse_u32(tokens[6], &full_duplex) != APP_OK)) {
          return APP_EINVAL;
        }
        if ((admin_up > 1u) ||
            (full_duplex > 1u) ||
            ((speed_mbps != LAN9645_CORE_PORT_SPEED_10_MBPS) &&
             (speed_mbps != LAN9645_CORE_PORT_SPEED_100_MBPS))) {
          return APP_EINVAL;
        }
        return cli_cmd_phylink_cpu_set(
          (uint8_t)cpu_port,
          (uint8_t)admin_up,
          (uint16_t)speed_mbps,
          (uint8_t)full_duplex
        );
      }

      return APP_EINVAL;
    }

    return APP_EINVAL;
  }

  if (cli_eq(tokens[0], "npi") != 0u) {
    if (ntok < 2u) {
      return APP_EINVAL;
    }

    if (cli_eq(tokens[1], "show") != 0u) {
      return cli_cmd_npi_show();
    }

    if ((cli_eq(tokens[1], "ext") != 0u) && (cli_eq(tokens[2], "set") != 0u)) {
      uint32_t npi_port = 0u;
      uint32_t ext_cpuq_mask = 0u;
      uint32_t ext_cpu_kill = 0u;
      uint32_t int_cpu_kill = 0u;
      if (ntok < 7u) {
        return APP_EINVAL;
      }
      if ((cli_parse_u32(tokens[3], &npi_port) != APP_OK) ||
          (cli_parse_u32(tokens[4], &ext_cpuq_mask) != APP_OK) ||
          (cli_parse_u32(tokens[5], &ext_cpu_kill) != APP_OK) ||
          (cli_parse_u32(tokens[6], &int_cpu_kill) != APP_OK)) {
        return APP_EINVAL;
      }
      if ((npi_port > LAN9645_NPI_PORT_MAX) ||
          (ext_cpuq_mask > LAN9645_NPI_CPUQ_MASK_MAX) ||
          (ext_cpu_kill > 1u) ||
          (int_cpu_kill > 1u)) {
        return APP_EINVAL;
      }
      return cli_cmd_npi_ext_set(
        (uint8_t)npi_port,
        (uint8_t)ext_cpuq_mask,
        (uint8_t)ext_cpu_kill,
        (uint8_t)int_cpu_kill
      );
    }

    if ((cli_eq(tokens[1], "ifh") != 0u) && (cli_eq(tokens[2], "set") != 0u)) {
      uint32_t incl_xtr_hdr = 0u;
      uint32_t incl_inj_hdr = 0u;
      if (ntok < 5u) {
        return APP_EINVAL;
      }
      if ((cli_parse_u32(tokens[3], &incl_xtr_hdr) != APP_OK) ||
          (cli_parse_u32(tokens[4], &incl_inj_hdr) != APP_OK)) {
        return APP_EINVAL;
      }
      if ((incl_xtr_hdr > LAN9645_NPI_IFH_PREFIX_MAX) || (incl_inj_hdr > LAN9645_NPI_IFH_PREFIX_MAX)) {
        return APP_EINVAL;
      }
      return cli_cmd_npi_ifh_set((uint8_t)incl_xtr_hdr, (uint8_t)incl_inj_hdr);
    }

    if ((cli_eq(tokens[1], "rew") != 0u) && (cli_eq(tokens[2], "set") != 0u)) {
      uint32_t rew_tag_cfg = 0u;
      if (ntok < 4u) {
        return APP_EINVAL;
      }
      if ((cli_parse_u32(tokens[3], &rew_tag_cfg) != APP_OK) ||
          (rew_tag_cfg > LAN9645_NPI_REW_TAG_CFG_MAX)) {
        return APP_EINVAL;
      }
      return cli_cmd_npi_rew_set((uint8_t)rew_tag_cfg);
    }

    if ((cli_eq(tokens[1], "srcpgid") != 0u) && (cli_eq(tokens[2], "set") != 0u)) {
      uint32_t src_pgid_mask = 0u;
      if (ntok < 4u) {
        return APP_EINVAL;
      }
      if ((cli_parse_u32(tokens[3], &src_pgid_mask) != APP_OK) ||
          (src_pgid_mask > LAN9645_NPI_PGID_MASK_MAX)) {
        return APP_EINVAL;
      }
      return cli_cmd_npi_srcpgid_set((uint16_t)src_pgid_mask);
    }

    return APP_EINVAL;
  }

  if (cli_eq(tokens[0], "hsr_prp") != 0u) {
    if (ntok < 2u) {
      return APP_EINVAL;
    }
    if (cli_eq(tokens[1], "show") != 0u) {
      return cli_cmd_hsr_prp_show();
    }
    if (cli_eq(tokens[1], "set") != 0u) {
      uint32_t pgid_cpu_mask = 0u;
      if (ntok < 3u) {
        return APP_EINVAL;
      }
      if ((cli_parse_u32(tokens[2], &pgid_cpu_mask) != APP_OK) ||
          (pgid_cpu_mask > LAN9645_HSR_PRP_PGID_MASK_MAX)) {
        return APP_EINVAL;
      }
      return cli_cmd_hsr_prp_set((uint16_t)pgid_cpu_mask);
    }
    return APP_EINVAL;
  }

  if (cli_eq(tokens[0], "lag") != 0u) {
    if (ntok < 2u) {
      return APP_EINVAL;
    }
    if (cli_eq(tokens[1], "show") != 0u) {
      return cli_cmd_lag_show();
    }
    if (cli_eq(tokens[1], "set") != 0u) {
      uint32_t aggr_mask = 0u;
      if (ntok < 3u) {
        return APP_EINVAL;
      }
      if (cli_parse_u32(tokens[2], &aggr_mask) != APP_OK) {
        return APP_EINVAL;
      }
      return cli_cmd_lag_set_mask(aggr_mask);
    }
    return APP_EINVAL;
  }

  if (cli_eq(tokens[0], "mdb") != 0u) {
    if (ntok < 3u) {
      return APP_EINVAL;
    }

    if (cli_eq(tokens[1], "cfg") != 0u) {
      uint32_t pgid_index = 0u;
      if (ntok < 4u) {
        return APP_EINVAL;
      }
      if ((cli_parse_u32(tokens[3], &pgid_index) != APP_OK) ||
          (pgid_index > LAN9645_MDB_PGID_INDEX_MAX)) {
        return APP_EINVAL;
      }
      if (cli_eq(tokens[2], "show") != 0u) {
        return cli_cmd_mdb_cfg_show((uint8_t)pgid_index);
      }
      if (cli_eq(tokens[2], "set") != 0u) {
        uint32_t san_ena = 0u;
        uint32_t cpuq_dst_pgid = 0u;
        uint32_t obey_vlan = 0u;
        if (ntok < 7u) {
          return APP_EINVAL;
        }
        if ((cli_parse_u32(tokens[4], &san_ena) != APP_OK) ||
            (cli_parse_u32(tokens[5], &cpuq_dst_pgid) != APP_OK) ||
            (cli_parse_u32(tokens[6], &obey_vlan) != APP_OK) ||
            (san_ena > 1u) ||
            (cpuq_dst_pgid > LAN9645_MDB_CPUQ_DST_MAX) ||
            (obey_vlan > 1u)) {
          return APP_EINVAL;
        }
        return cli_cmd_mdb_cfg_set(
          (uint8_t)pgid_index,
          (uint8_t)san_ena,
          (uint8_t)cpuq_dst_pgid,
          (uint8_t)obey_vlan
        );
      }
      return APP_EINVAL;
    }

    if (cli_eq(tokens[1], "mask") != 0u) {
      uint32_t pgid_index = 0u;
      if (ntok < 4u) {
        return APP_EINVAL;
      }
      if ((cli_parse_u32(tokens[3], &pgid_index) != APP_OK) ||
          (pgid_index > LAN9645_MDB_PGID_INDEX_MAX)) {
        return APP_EINVAL;
      }
      if (cli_eq(tokens[2], "show") != 0u) {
        return cli_cmd_mdb_mask_show((uint8_t)pgid_index);
      }
      if (cli_eq(tokens[2], "set") != 0u) {
        uint32_t member_mask = 0u;
        if (ntok < 5u) {
          return APP_EINVAL;
        }
        if ((cli_parse_u32(tokens[4], &member_mask) != APP_OK) ||
            (member_mask > LAN9645_MDB_MEMBER_MASK_MAX)) {
          return APP_EINVAL;
        }
        return cli_cmd_mdb_mask_set((uint8_t)pgid_index, (uint16_t)member_mask);
      }
      return APP_EINVAL;
    }

    if (cli_eq(tokens[1], "ipmc") != 0u) {
      if (cli_eq(tokens[2], "show") != 0u) {
        return cli_cmd_mdb_ipmc_show();
      }
      if (cli_eq(tokens[2], "set") != 0u) {
        uint32_t mcipv4_mask = 0u;
        uint32_t mcipv6_mask = 0u;
        if (ntok < 5u) {
          return APP_EINVAL;
        }
        if ((cli_parse_u32(tokens[3], &mcipv4_mask) != APP_OK) ||
            (cli_parse_u32(tokens[4], &mcipv6_mask) != APP_OK) ||
            (mcipv4_mask > LAN9645_MDB_MEMBER_MASK_MAX) ||
            (mcipv6_mask > LAN9645_MDB_MEMBER_MASK_MAX)) {
          return APP_EINVAL;
        }
        return cli_cmd_mdb_ipmc_set((uint16_t)mcipv4_mask, (uint16_t)mcipv6_mask);
      }
      return APP_EINVAL;
    }

    return APP_EINVAL;
  }

  if (cli_eq(tokens[0], "mrp") != 0u) {
    if (ntok < 2u) {
      return APP_EINVAL;
    }
    if (cli_eq(tokens[1], "show") != 0u) {
      return cli_cmd_mrp_show();
    }
    if (cli_eq(tokens[1], "set") != 0u) {
      uint32_t pgid_mrp_mask = 0u;
      if (ntok < 3u) {
        return APP_EINVAL;
      }
      if ((cli_parse_u32(tokens[2], &pgid_mrp_mask) != APP_OK) ||
          (pgid_mrp_mask > LAN9645_MRP_MEMBER_MASK_MAX)) {
        return APP_EINVAL;
      }
      return cli_cmd_mrp_set((uint16_t)pgid_mrp_mask);
    }
    return APP_EINVAL;
  }

  if (cli_eq(tokens[0], "ptp") != 0u) {
    if (ntok < 2u) {
      return APP_EINVAL;
    }
    if (cli_eq(tokens[1], "show") != 0u) {
      return cli_cmd_ptp_show();
    }
    if (cli_eq(tokens[1], "global") != 0u) {
      uint32_t enable = 0u;
      if (ntok < 3u) {
        return APP_EINVAL;
      }
      if ((cli_parse_u32(tokens[2], &enable) != APP_OK) || (enable > LAN9645_PTP_BOOL_MAX)) {
        return APP_EINVAL;
      }
      return cli_cmd_ptp_set_global((uint8_t)enable);
    }
    if (cli_eq(tokens[1], "wait") != 0u) {
      uint32_t port = 0u;
      uint32_t wait_for_ts = 0u;
      if (ntok < 4u) {
        return APP_EINVAL;
      }
      if ((cli_parse_u32(tokens[2], &port) != APP_OK) ||
          (cli_parse_u32(tokens[3], &wait_for_ts) != APP_OK) ||
          (cli_is_managed_port(port) == 0u) ||
          (wait_for_ts > LAN9645_PTP_BOOL_MAX)) {
        return APP_EINVAL;
      }
      return cli_cmd_ptp_set_wait((uint8_t)port, (uint8_t)wait_for_ts);
    }
    return APP_EINVAL;
  }

  if (cli_eq(tokens[0], "vcap") != 0u) {
    uint32_t port = 0u;
    if (ntok < 3u) {
      return APP_EINVAL;
    }
    if ((cli_parse_u32(tokens[2], &port) != APP_OK) ||
        (cli_is_managed_port(port) == 0u)) {
      return APP_EINVAL;
    }

    if (cli_eq(tokens[1], "show") != 0u) {
      return cli_cmd_vcap_show((uint8_t)port);
    }
    if (cli_eq(tokens[1], "set") != 0u) {
      uint32_t s1_raw = 0u;
      uint32_t s2_raw = 0u;
      if (ntok < 5u) {
        return APP_EINVAL;
      }
      if ((cli_parse_u32(tokens[3], &s1_raw) != APP_OK) ||
          (cli_parse_u32(tokens[4], &s2_raw) != APP_OK)) {
        return APP_EINVAL;
      }
      return cli_cmd_vcap_set_raw((uint8_t)port, s1_raw, s2_raw);
    }
    return APP_EINVAL;
  }

  if (cli_eq(tokens[0], "tcflower") != 0u) {
    uint32_t port = 0u;
    if (ntok < 3u) {
      return APP_EINVAL;
    }
    if ((cli_parse_u32(tokens[2], &port) != APP_OK) ||
        (cli_is_managed_port(port) == 0u)) {
      return APP_EINVAL;
    }

    if (cli_eq(tokens[1], "show") != 0u) {
      return cli_cmd_tcflower_show((uint8_t)port);
    }
    if (cli_eq(tokens[1], "set") != 0u) {
      uint32_t enable = 0u;
      if (ntok < 4u) {
        return APP_EINVAL;
      }
      if ((cli_parse_u32(tokens[3], &enable) != APP_OK) || (enable > 1u)) {
        return APP_EINVAL;
      }
      return cli_cmd_tcflower_set((uint8_t)port, (uint8_t)enable);
    }
    return APP_EINVAL;
  }

  if (cli_eq(tokens[0], "tcmatch") != 0u) {
    uint32_t port = 0u;
    if (ntok < 3u) {
      return APP_EINVAL;
    }
    if ((cli_parse_u32(tokens[2], &port) != APP_OK) ||
        (cli_is_managed_port(port) == 0u)) {
      return APP_EINVAL;
    }

    if (cli_eq(tokens[1], "show") != 0u) {
      return cli_cmd_tcmatch_show((uint8_t)port);
    }
    if (cli_eq(tokens[1], "set") != 0u) {
      uint32_t enable = 0u;
      if (ntok < 4u) {
        return APP_EINVAL;
      }
      if ((cli_parse_u32(tokens[3], &enable) != APP_OK) || (enable > 1u)) {
        return APP_EINVAL;
      }
      return cli_cmd_tcmatch_set((uint8_t)port, (uint8_t)enable);
    }
    return APP_EINVAL;
  }

  if (cli_eq(tokens[0], "cnt") != 0u) {
    uint32_t view = 0u;
    if (ntok < 3u) {
      return APP_EINVAL;
    }
    if (cli_parse_u32(tokens[2], &view) != APP_OK) {
      return APP_EINVAL;
    }
    if (view > LAN9645_STATS_VIEW_MAX) {
      return APP_EINVAL;
    }

    if (cli_eq(tokens[1], "read") != 0u) {
      return cli_cmd_cnt_read((uint16_t)view);
    }
    if (cli_eq(tokens[1], "clear") != 0u) {
      return cli_cmd_cnt_clear((uint16_t)view);
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
  if (st != APP_OK) {
    s_cli.command_error_count++;
  }
  cli_emit_result(st);
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
