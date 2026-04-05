#include "lan9645/lan9645_stats.h"

/**
 * @file lan9645_stats.c
 * @brief Baseline SYS counter read/clear helpers for CLI diagnostics.
 */

#include <string.h>

#include "diag/diag_core.h"
#include "diag/diag_router.h"
#include "lan9645/lan9645_reg.h"
#include "lan9645/lan9645_regs.h"

#define LAN9645_STATS_CNT_RX_OCT_OFFSET   (0x000u)
#define LAN9645_STATS_CNT_TX_OCT_OFFSET   (0x080u)
#define LAN9645_STATS_CNT_DR_LOCAL_OFFSET (0x100u)

static uint8_t s_stats_initialized = 0u;

/**
 * @brief Reports one stats-operation failure to diagnostics and serial logs.
 *
 * @param[in] view Active stats view tied to the failure.
 * @param[in] reg_tag Register-relative address tag.
 * @param[in] st Failure status code.
 * @param[in] step Internal step tag for deterministic troubleshooting.
 */
static void lan9645_stats_report_fail(uint16_t view, uint32_t reg_tag, app_status_t st, uint32_t step)
{
  diag_event_t evt = {0};

  evt.id = DIAG_EVT_LAN_CNT_OP_FAIL;
  evt.severity = DIAG_SEV_ERR;
  evt.dev = (uint32_t)view;
  evt.reg = reg_tag;
  evt.arg0 = (uint32_t)(int32_t)st;
  evt.arg1 = step;

  (void)diag_core_report(&evt);
  (void)diag_router_write_event(&evt);
}

/**
 * @brief Validates context and view for stats operations.
 */
static app_status_t lan9645_stats_validate(const lan9645_core_t *core, uint16_t view)
{
  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if (view > LAN9645_STATS_VIEW_MAX) {
    return APP_EINVAL;
  }
  return APP_OK;
}

/**
 * @brief Programs `SYS_STAT_CFG` for read or clear operations and verifies readback.
 */
static app_status_t lan9645_stats_program_cfg(lan9645_core_t *core, uint16_t view, uint8_t clear_sel, uint32_t step_base)
{
  uint32_t wr = 0u;
  uint32_t rd = 0u;
  uint32_t rd_view = 0u;
  uint32_t rd_clear = 0u;
  uint32_t reg_tag = lan9645_reg_desc_rel_addr(&LAN9645_REG_SYS_STAT_CFG, 0u, 0u);
  app_status_t st = APP_OK;

  wr = LAN9645_SYS_STAT_CFG_STAT_VIEW_SET((uint32_t)view) |
       LAN9645_SYS_STAT_CFG_STAT_CLEAR_SHOT_SET((uint32_t)clear_sel);

  st = lan9645_reg_write32_desc(core->reg, &LAN9645_REG_SYS_STAT_CFG, 0u, 0u, 0u, wr);
  if (st != APP_OK) {
    lan9645_stats_report_fail(view, reg_tag, st, step_base + 0u);
    return st;
  }

  st = lan9645_reg_read32_desc(core->reg, &LAN9645_REG_SYS_STAT_CFG, 0u, 0u, 0u, &rd);
  if (st != APP_OK) {
    lan9645_stats_report_fail(view, reg_tag, st, step_base + 1u);
    return st;
  }

  rd_view = LAN9645_FIELD_GET(LAN9645_SYS_STAT_CFG_STAT_VIEW, rd);
  rd_clear = LAN9645_FIELD_GET(LAN9645_SYS_STAT_CFG_STAT_CLEAR_SHOT, rd);
  if (rd_view != (uint32_t)view) {
    lan9645_stats_report_fail(view, reg_tag, APP_EIO, step_base + 2u);
    return APP_EIO;
  }

  /* CLEAR_SHOT may self-clear depending on timing; accept either state. */
  if ((rd_clear != 0u) && (rd_clear != (uint32_t)clear_sel)) {
    lan9645_stats_report_fail(view, reg_tag, APP_EIO, step_base + 3u);
    return APP_EIO;
  }

  return APP_OK;
}

/**
 * @brief Reads one `SYS_CNT` register at a selected counter offset.
 */
static app_status_t lan9645_stats_read_cnt(lan9645_core_t *core, uint16_t view, uint32_t offset, uint32_t *out, uint32_t step)
{
  app_status_t st = APP_OK;

  if (out == NULL) {
    return APP_EINVAL;
  }

  st = lan9645_reg_read32_desc(core->reg, &LAN9645_REG_SYS_CNT, 0u, offset, 0u, out);
  if (st != APP_OK) {
    lan9645_stats_report_fail(view, lan9645_reg_desc_rel_addr(&LAN9645_REG_SYS_CNT, offset, 0u), st, step);
  }
  return st;
}

/**
 * @brief Initializes the stats service.
 */
app_status_t lan9645_stats_init(void)
{
  s_stats_initialized = 1u;
  return APP_OK;
}

/**
 * @brief Reads baseline SYS counters for one selected view.
 */
app_status_t lan9645_stats_read(lan9645_core_t *core, uint16_t view, lan9645_stats_sample_t *out)
{
  app_status_t st = APP_OK;
  uint32_t sw = 0u;

  if (out == NULL) {
    return APP_EINVAL;
  }
  st = lan9645_stats_validate(core, view);
  if (st != APP_OK) {
    return st;
  }
  if (s_stats_initialized == 0u) {
    (void)lan9645_stats_init();
  }

  st = lan9645_stats_program_cfg(core, view, 0u, 10u);
  if (st != APP_OK) {
    return st;
  }

  memset(out, 0, sizeof(*out));
  out->view = view;

  st = lan9645_stats_read_cnt(core, view, LAN9645_STATS_CNT_RX_OCT_OFFSET, &out->rx_oct, 20u);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_stats_read_cnt(core, view, LAN9645_STATS_CNT_TX_OCT_OFFSET, &out->tx_oct, 21u);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_stats_read_cnt(core, view, LAN9645_STATS_CNT_DR_LOCAL_OFFSET, &out->dr_local, 22u);
  if (st != APP_OK) {
    return st;
  }

  if (view < 10u) {
    st = lan9645_reg_read32_desc(core->reg, &LAN9645_REG_SYS_SW_STATUS, 0u, 0u, (uint32_t)view, &sw);
    if (st != APP_OK) {
      lan9645_stats_report_fail(view, lan9645_reg_desc_rel_addr(&LAN9645_REG_SYS_SW_STATUS, 0u, (uint32_t)view), st, 23u);
      return st;
    }
    out->sw_paused = ((sw & LAN9645_SYS_SW_STATUS_PORT_RX_PAUSED) != 0u) ? 1u : 0u;
  }

  return APP_OK;
}

/**
 * @brief Clears baseline port counters for one selected view.
 */
app_status_t lan9645_stats_clear(lan9645_core_t *core, uint16_t view)
{
  app_status_t st = lan9645_stats_validate(core, view);
  if (st != APP_OK) {
    return st;
  }
  if (s_stats_initialized == 0u) {
    (void)lan9645_stats_init();
  }

  return lan9645_stats_program_cfg(core, view, LAN9645_STATS_CLEAR_SEL_PORTS, 30u);
}
