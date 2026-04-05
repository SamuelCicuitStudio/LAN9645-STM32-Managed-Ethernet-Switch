#include "lan9645/lan9645_vlan.h"

/**
 * @file lan9645_vlan.c
 * @brief Minimal VLAN baseline control using ANA_PGID/ANA_PGID_CFG.
 */

#include <string.h>

#include "diag/diag_core.h"
#include "diag/diag_router.h"
#include "lan9645/lan9645_reg.h"
#include "lan9645/lan9645_regs.h"

typedef struct {
  uint8_t configured;
  uint16_t untag_mask;
} lan9645_vlan_cache_entry_t;

static uint8_t s_vlan_initialized = 0u;
static lan9645_vlan_cache_entry_t s_vlan_cache[LAN9645_VLAN_VID_MAX + 1u];

/**
 * @brief Reports one VLAN configuration failure into diagnostics and routed logs.
 *
 * @param[in] vid VLAN identifier associated with the failure.
 * @param[in] reg_tag Register-relative address tag.
 * @param[in] st Failure status code.
 * @param[in] step Internal step index for deterministic troubleshooting.
 */
static void lan9645_vlan_report_fail(uint16_t vid, uint32_t reg_tag, app_status_t st, uint32_t step)
{
  diag_event_t evt = {0};

  evt.id = DIAG_EVT_LAN_VLAN_CFG_FAIL;
  evt.severity = DIAG_SEV_ERR;
  evt.dev = (uint32_t)vid;
  evt.reg = reg_tag;
  evt.arg0 = (uint32_t)(int32_t)st;
  evt.arg1 = step;

  (void)diag_core_report(&evt);
  (void)diag_router_write_event(&evt);
}

/**
 * @brief Validates inputs for VLAN set/get operations.
 */
static app_status_t lan9645_vlan_validate_args(
  const lan9645_core_t *core,
  uint16_t vid,
  uint16_t member_mask,
  uint16_t untag_mask
)
{
  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  if (vid > LAN9645_VLAN_VID_MAX) {
    return APP_EINVAL;
  }
  if ((member_mask & (uint16_t)(~LAN9645_VLAN_MEMBER_MASK_ALL)) != 0u) {
    return APP_EINVAL;
  }
  if ((untag_mask & (uint16_t)(~LAN9645_VLAN_MEMBER_MASK_ALL)) != 0u) {
    return APP_EINVAL;
  }
  if ((untag_mask & (uint16_t)(~member_mask)) != 0u) {
    return APP_EINVAL;
  }
  return APP_OK;
}

/**
 * @brief Ensures aggregation hash baseline remains deterministic for managed mode.
 */
static app_status_t lan9645_vlan_ensure_aggr_cfg(lan9645_core_t *core)
{
  const uint32_t mask =
    LAN9645_ANA_AGGR_CFG_AC_RND_ENA |
    LAN9645_ANA_AGGR_CFG_AC_DMAC_ENA |
    LAN9645_ANA_AGGR_CFG_AC_SMAC_ENA |
    LAN9645_ANA_AGGR_CFG_AC_IP6_FLOW_LBL_ENA |
    LAN9645_ANA_AGGR_CFG_AC_IP6_TCPUDP_ENA |
    LAN9645_ANA_AGGR_CFG_AC_IP4_SIPDIP_ENA |
    LAN9645_ANA_AGGR_CFG_AC_IP4_TCPUDP_ENA;
  const uint32_t value =
    LAN9645_ANA_AGGR_CFG_AC_DMAC_ENA |
    LAN9645_ANA_AGGR_CFG_AC_SMAC_ENA;
  uint32_t rd = 0u;
  app_status_t st = APP_OK;

  st = lan9645_reg_rmw32_desc(core->reg, &LAN9645_REG_ANA_AGGR_CFG, 0u, 0u, 0u, mask, value);
  if (st != APP_OK) {
    lan9645_vlan_report_fail(0u, lan9645_reg_desc_rel_addr(&LAN9645_REG_ANA_AGGR_CFG, 0u, 0u), st, 1u);
    return st;
  }

  st = lan9645_reg_read32_desc(core->reg, &LAN9645_REG_ANA_AGGR_CFG, 0u, 0u, 0u, &rd);
  if (st != APP_OK) {
    lan9645_vlan_report_fail(0u, lan9645_reg_desc_rel_addr(&LAN9645_REG_ANA_AGGR_CFG, 0u, 0u), st, 2u);
    return st;
  }
  if ((rd & mask) != value) {
    lan9645_vlan_report_fail(0u, lan9645_reg_desc_rel_addr(&LAN9645_REG_ANA_AGGR_CFG, 0u, 0u), APP_EIO, 3u);
    return APP_EIO;
  }

  return APP_OK;
}

/**
 * @brief Initializes VLAN cache state.
 */
app_status_t lan9645_vlan_init(void)
{
  memset(s_vlan_cache, 0, sizeof(s_vlan_cache));
  s_vlan_initialized = 1u;
  return APP_OK;
}

/**
 * @brief Applies one baseline VLAN entry using PGID and PGID_CFG.
 */
app_status_t lan9645_vlan_set(
  lan9645_core_t *core,
  uint16_t vid,
  uint16_t member_mask,
  uint16_t untag_mask
)
{
  const uint32_t pgid_mask = LAN9645_ANA_PGID_PGID;
  const uint32_t pgid_value = LAN9645_ANA_PGID_PGID_SET((uint32_t)member_mask);
  const uint32_t pgid_cfg_mask =
    LAN9645_ANA_PGID_CFG_SAN_ENA |
    LAN9645_ANA_PGID_CFG_CPUQ_DST_PGID |
    LAN9645_ANA_PGID_CFG_OBEY_VLAN;
  const uint32_t pgid_cfg_value =
    LAN9645_ANA_PGID_CFG_CPUQ_DST_PGID_SET(0u) |
    LAN9645_ANA_PGID_CFG_OBEY_VLAN;
  uint32_t rd = 0u;
  uint32_t pgid_reg_tag = 0u;
  uint32_t pgid_cfg_reg_tag = 0u;
  app_status_t st = APP_OK;

  st = lan9645_vlan_validate_args(core, vid, member_mask, untag_mask);
  if (st != APP_OK) {
    return st;
  }
  if (s_vlan_initialized == 0u) {
    (void)lan9645_vlan_init();
  }

  pgid_reg_tag = lan9645_reg_desc_rel_addr(&LAN9645_REG_ANA_PGID, (uint32_t)vid, 0u);
  pgid_cfg_reg_tag = lan9645_reg_desc_rel_addr(&LAN9645_REG_ANA_PGID_CFG, (uint32_t)vid, 0u);

  st = lan9645_vlan_ensure_aggr_cfg(core);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_reg_rmw32_desc(core->reg, &LAN9645_REG_ANA_PGID, 0u, (uint32_t)vid, 0u, pgid_mask, pgid_value);
  if (st != APP_OK) {
    lan9645_vlan_report_fail(vid, pgid_reg_tag, st, 4u);
    return st;
  }

  st = lan9645_reg_read32_desc(core->reg, &LAN9645_REG_ANA_PGID, 0u, (uint32_t)vid, 0u, &rd);
  if (st != APP_OK) {
    lan9645_vlan_report_fail(vid, pgid_reg_tag, st, 5u);
    return st;
  }
  if ((rd & pgid_mask) != pgid_value) {
    lan9645_vlan_report_fail(vid, pgid_reg_tag, APP_EIO, 6u);
    return APP_EIO;
  }

  st = lan9645_reg_rmw32_desc(core->reg, &LAN9645_REG_ANA_PGID_CFG, 0u, (uint32_t)vid, 0u, pgid_cfg_mask, pgid_cfg_value);
  if (st != APP_OK) {
    lan9645_vlan_report_fail(vid, pgid_cfg_reg_tag, st, 7u);
    return st;
  }

  st = lan9645_reg_read32_desc(core->reg, &LAN9645_REG_ANA_PGID_CFG, 0u, (uint32_t)vid, 0u, &rd);
  if (st != APP_OK) {
    lan9645_vlan_report_fail(vid, pgid_cfg_reg_tag, st, 8u);
    return st;
  }
  if ((rd & pgid_cfg_mask) != pgid_cfg_value) {
    lan9645_vlan_report_fail(vid, pgid_cfg_reg_tag, APP_EIO, 9u);
    return APP_EIO;
  }

  s_vlan_cache[vid].configured = 1u;
  s_vlan_cache[vid].untag_mask = untag_mask;
  return APP_OK;
}

/**
 * @brief Reads one baseline VLAN entry snapshot.
 */
app_status_t lan9645_vlan_get(lan9645_core_t *core, uint16_t vid, lan9645_vlan_entry_t *out)
{
  uint32_t pgid = 0u;
  uint32_t pgid_cfg = 0u;
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL) || (out == NULL)) {
    return APP_EINVAL;
  }
  if (vid > LAN9645_VLAN_VID_MAX) {
    return APP_EINVAL;
  }
  if (s_vlan_initialized == 0u) {
    (void)lan9645_vlan_init();
  }

  st = lan9645_reg_read32_desc(core->reg, &LAN9645_REG_ANA_PGID, 0u, (uint32_t)vid, 0u, &pgid);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_reg_read32_desc(core->reg, &LAN9645_REG_ANA_PGID_CFG, 0u, (uint32_t)vid, 0u, &pgid_cfg);
  if (st != APP_OK) {
    return st;
  }

  memset(out, 0, sizeof(*out));
  out->configured = s_vlan_cache[vid].configured;
  out->obey_vlan = ((pgid_cfg & LAN9645_ANA_PGID_CFG_OBEY_VLAN) != 0u) ? 1u : 0u;
  out->member_mask = (uint16_t)LAN9645_FIELD_GET(LAN9645_ANA_PGID_PGID, pgid);
  out->untag_mask = s_vlan_cache[vid].untag_mask;
  return APP_OK;
}
