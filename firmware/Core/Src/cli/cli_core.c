#include "cli/cli_core.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app/app_orchestrator.h"
#include "diag/diag_core.h"
#include "diag/diag_router.h"
#include "lan9645/lan9645_reg.h"
#include "lan9645/lan9645_runtime.h"
#include "lan9645/lan9645_user_phy_adapter.h"

#define CLI_MAX_LINE_LEN      (160u)
#define CLI_MAX_TOKENS        (8u)

static cli_core_state_t s_cli = {0};
static lan9645_user_phy_adapter_t s_cli_phy = {0};
static uint8_t s_cli_phy_ready = 0u;

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

static const char *cli_diag_evt_to_str(diag_event_id_t id)
{
  switch (id) {
    case DIAG_EVT_BOOT: return "BOOT";
    case DIAG_EVT_LAN_SPI_IFSTAT: return "LAN_SPI_IFSTAT";
    case DIAG_EVT_LAN_SPI_TIMEOUT: return "LAN_SPI_TIMEOUT";
    case DIAG_EVT_LAN_STRAP_EXPECTATION: return "LAN_STRAP_EXPECTATION";
    case DIAG_EVT_LAN_RMII_LINK_TIMEOUT: return "LAN_RMII_LINK_TIMEOUT";
    case DIAG_EVT_LAN_SERDES0_LINK_TIMEOUT: return "LAN_SERDES0_LINK_TIMEOUT";
    case DIAG_EVT_LAN_SERDES1_LINK_TIMEOUT: return "LAN_SERDES1_LINK_TIMEOUT";
    case DIAG_EVT_LAN_IRQ_STUCK: return "LAN_IRQ_STUCK";
    case DIAG_EVT_LAN_DROP_COUNTER_ALERT: return "LAN_DROP_COUNTER_ALERT";
    default: return "UNKNOWN";
  }
}

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

static app_status_t cli_cmd_help(void)
{
  (void)cli_writef("cli commands:\r\n");
  (void)cli_writef("  help\r\n");
  (void)cli_writef("  state show\r\n");
  (void)cli_writef("  status all\r\n");
  (void)cli_writef("  status errors\r\n");
  (void)cli_writef("  diag clear\r\n");
  (void)cli_writef("  link show [0|1]\r\n");
  (void)cli_writef("  phy show [0|1]\r\n");
  (void)cli_writef("  port show\r\n");
  (void)cli_writef("  port select <0|1>\r\n");
  (void)cli_writef("  reg read <0xADDR>\r\n");
  (void)cli_writef("  reg write <0xADDR> <0xVALUE>\r\n");
  (void)cli_writef("  sanity spi <0xADDR> [count]\r\n");
  return APP_OK;
}

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

static app_status_t cli_cmd_diag_clear(void)
{
  app_status_t st = diag_core_clear_all();
  if (st != APP_OK) {
    return st;
  }

  (void)cli_writef("diag cleared\r\n");
  return APP_OK;
}

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

static app_status_t cli_cmd_status_all(void)
{
  lan9645_core_t *core = lan9645_runtime_get_core();
  diag_event_t last = {0};
  app_orchestrator_snapshot_t snap = {0};

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
    if (lan9645_core_read_feat_disable(core, &feat) == APP_OK) {
      (void)cli_writef("lan feat_disable=0x%08lX\r\n", (unsigned long)feat);
    } else {
      (void)cli_writef("lan feat_disable=unavailable\r\n");
    }
  } else {
    (void)cli_writef("lan core not ready\r\n");
  }

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

  if (cli_eq(tokens[0], "port") != 0u) {
    if (ntok < 2u) {
      return APP_EINVAL;
    }
    if (cli_eq(tokens[1], "show") != 0u) {
      (void)cli_writef("active port=%u\r\n", (unsigned int)s_cli.active_port);
      return APP_OK;
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
    return APP_EINVAL;
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

app_status_t cli_core_init(void)
{
  memset(&s_cli, 0, sizeof(s_cli));
  s_cli.initialized = 1u;
  s_cli.active_port = LAN9645_USER_PHY_PORT0;
  s_cli_phy_ready = 0u;
  (void)cli_writef("cli ready\r\n");
  return APP_OK;
}

app_status_t cli_core_poll(void)
{
  if (s_cli.initialized == 0u) {
    (void)cli_core_init();
  }
  return APP_OK;
}

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
