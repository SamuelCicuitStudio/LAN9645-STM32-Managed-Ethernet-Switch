#include "lan9645/lan9645_bum.h"

/**
 * @file lan9645_bum.c
 * @brief Baseline BUM flooding configuration aligned with Linux LAN9645 setup.
 */

#include "lan9645/lan9645_reg.h"
#include "lan9645/lan9645_regs.h"

#define LAN9645_BUM_FRONT_PORT_MASK          (0x103u) /* ports 0,1,8 */

#define LAN9645_BUM_PGID_AGGR                (64u)
#define LAN9645_BUM_PGID_UC                  (LAN9645_BUM_PGID_AGGR - 5u) /* 59 */
#define LAN9645_BUM_PGID_BC                  (LAN9645_BUM_PGID_AGGR - 4u) /* 60 */
#define LAN9645_BUM_PGID_MC                  (LAN9645_BUM_PGID_AGGR - 3u) /* 61 */
#define LAN9645_BUM_PGID_MCIPV4              (LAN9645_BUM_PGID_AGGR - 2u) /* 62 */
#define LAN9645_BUM_PGID_MCIPV6              (LAN9645_BUM_PGID_AGGR - 1u) /* 63 */

static uint8_t s_bum_initialized = 0u;

/**
 * @brief Validates that BUM operations can access a ready core/register layer.
 */
static app_status_t lan9645_bum_validate_core(const lan9645_core_t *core)
{
  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }
  return APP_OK;
}

/**
 * @brief Validates one flooding-priority index.
 */
static app_status_t lan9645_bum_validate_priority(uint8_t priority)
{
  if (priority >= LAN9645_BUM_PRIO_COUNT) {
    return APP_EINVAL;
  }
  return APP_OK;
}

/**
 * @brief Validates one PGID value encoded in flooding fields.
 */
static app_status_t lan9645_bum_validate_pgid(uint8_t pgid)
{
  if (pgid > LAN9645_BUM_PGID_MAX) {
    return APP_EINVAL;
  }
  return APP_OK;
}

/**
 * @brief Writes one descriptor register and verifies masked readback.
 */
static app_status_t lan9645_bum_write_verify(
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
 * @brief Initializes BUM baseline service state.
 */
app_status_t lan9645_bum_init(void)
{
  s_bum_initialized = 1u;
  return APP_OK;
}

/**
 * @brief Reads one flooding-priority entry.
 */
app_status_t lan9645_bum_get_flood_cfg(
  lan9645_core_t *core,
  uint8_t priority,
  lan9645_bum_flood_cfg_t *out
)
{
  uint32_t rd = 0u;
  app_status_t st = lan9645_bum_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_bum_validate_priority(priority);
  if (st != APP_OK) {
    return st;
  }
  if (out == NULL) {
    return APP_EINVAL;
  }

  st = lan9645_reg_read32_desc(core->reg, &LAN9645_REG_ANA_FLOODING, 0u, 0u, (uint32_t)priority, &rd);
  if (st != APP_OK) {
    return st;
  }

  out->priority = priority;
  out->pgid_uc = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_FLOODING_FLD_UNICAST, rd);
  out->pgid_bc = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_FLOODING_FLD_BROADCAST, rd);
  out->pgid_mc = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_FLOODING_FLD_MULTICAST, rd);
  return APP_OK;
}

/**
 * @brief Writes one flooding-priority entry with readback verification.
 */
app_status_t lan9645_bum_set_flood_cfg(
  lan9645_core_t *core,
  uint8_t priority,
  uint8_t pgid_uc,
  uint8_t pgid_bc,
  uint8_t pgid_mc
)
{
  const uint32_t flood_mask =
    LAN9645_ANA_FLOODING_FLD_MULTICAST |
    LAN9645_ANA_FLOODING_FLD_UNICAST |
    LAN9645_ANA_FLOODING_FLD_BROADCAST;
  const uint32_t flood_value =
    LAN9645_ANA_FLOODING_FLD_MULTICAST_SET((uint32_t)pgid_mc) |
    LAN9645_ANA_FLOODING_FLD_UNICAST_SET((uint32_t)pgid_uc) |
    LAN9645_ANA_FLOODING_FLD_BROADCAST_SET((uint32_t)pgid_bc);
  app_status_t st = lan9645_bum_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_bum_validate_priority(priority);
  if (st != APP_OK) {
    return st;
  }
  if ((lan9645_bum_validate_pgid(pgid_uc) != APP_OK) ||
      (lan9645_bum_validate_pgid(pgid_bc) != APP_OK) ||
      (lan9645_bum_validate_pgid(pgid_mc) != APP_OK)) {
    return APP_EINVAL;
  }

  return lan9645_bum_write_verify(
    core,
    &LAN9645_REG_ANA_FLOODING,
    0u,
    (uint32_t)priority,
    flood_value,
    flood_mask
  );
}

/**
 * @brief Reads IPMC flooding configuration.
 */
app_status_t lan9645_bum_get_ipmc_cfg(lan9645_core_t *core, lan9645_bum_ipmc_cfg_t *out)
{
  uint32_t rd = 0u;
  app_status_t st = lan9645_bum_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (out == NULL) {
    return APP_EINVAL;
  }

  st = lan9645_reg_read32_desc(core->reg, &LAN9645_REG_ANA_FLOODING_IPMC, 0u, 0u, 0u, &rd);
  if (st != APP_OK) {
    return st;
  }

  out->pgid_mc4_ctrl = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_FLOODING_IPMC_FLD_MC4_CTRL, rd);
  out->pgid_mc4_data = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_FLOODING_IPMC_FLD_MC4_DATA, rd);
  out->pgid_mc6_ctrl = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_FLOODING_IPMC_FLD_MC6_CTRL, rd);
  out->pgid_mc6_data = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_FLOODING_IPMC_FLD_MC6_DATA, rd);
  return APP_OK;
}

/**
 * @brief Writes IPMC flooding configuration with readback verification.
 */
app_status_t lan9645_bum_set_ipmc_cfg(
  lan9645_core_t *core,
  uint8_t pgid_mc4_ctrl,
  uint8_t pgid_mc4_data,
  uint8_t pgid_mc6_ctrl,
  uint8_t pgid_mc6_data
)
{
  const uint32_t flood_ipmc_mask =
    LAN9645_ANA_FLOODING_IPMC_FLD_MC4_DATA |
    LAN9645_ANA_FLOODING_IPMC_FLD_MC4_CTRL |
    LAN9645_ANA_FLOODING_IPMC_FLD_MC6_DATA |
    LAN9645_ANA_FLOODING_IPMC_FLD_MC6_CTRL;
  const uint32_t flood_ipmc_value =
    LAN9645_ANA_FLOODING_IPMC_FLD_MC4_DATA_SET((uint32_t)pgid_mc4_data) |
    LAN9645_ANA_FLOODING_IPMC_FLD_MC4_CTRL_SET((uint32_t)pgid_mc4_ctrl) |
    LAN9645_ANA_FLOODING_IPMC_FLD_MC6_DATA_SET((uint32_t)pgid_mc6_data) |
    LAN9645_ANA_FLOODING_IPMC_FLD_MC6_CTRL_SET((uint32_t)pgid_mc6_ctrl);
  app_status_t st = lan9645_bum_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if ((lan9645_bum_validate_pgid(pgid_mc4_ctrl) != APP_OK) ||
      (lan9645_bum_validate_pgid(pgid_mc4_data) != APP_OK) ||
      (lan9645_bum_validate_pgid(pgid_mc6_ctrl) != APP_OK) ||
      (lan9645_bum_validate_pgid(pgid_mc6_data) != APP_OK)) {
    return APP_EINVAL;
  }

  return lan9645_bum_write_verify(
    core,
    &LAN9645_REG_ANA_FLOODING_IPMC,
    0u,
    0u,
    flood_ipmc_value,
    flood_ipmc_mask
  );
}

/**
 * @brief Reads one `ANA_PGID` destination member mask.
 */
app_status_t lan9645_bum_get_pgid_mask(
  lan9645_core_t *core,
  uint8_t pgid,
  uint16_t *member_mask
)
{
  uint32_t rd = 0u;
  app_status_t st = lan9645_bum_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_bum_validate_pgid(pgid);
  if (st != APP_OK) {
    return st;
  }
  if (member_mask == NULL) {
    return APP_EINVAL;
  }

  st = lan9645_reg_read32_desc(core->reg, &LAN9645_REG_ANA_PGID, 0u, (uint32_t)pgid, 0u, &rd);
  if (st != APP_OK) {
    return st;
  }

  *member_mask = (uint16_t)LAN9645_FIELD_GET(LAN9645_ANA_PGID_PGID, rd);
  return APP_OK;
}

/**
 * @brief Writes one `ANA_PGID` destination member mask with readback verification.
 */
app_status_t lan9645_bum_set_pgid_mask(
  lan9645_core_t *core,
  uint8_t pgid,
  uint16_t member_mask
)
{
  const uint32_t pgid_mask = LAN9645_ANA_PGID_PGID;
  app_status_t st = lan9645_bum_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_bum_validate_pgid(pgid);
  if (st != APP_OK) {
    return st;
  }
  if (member_mask > LAN9645_BUM_MEMBER_MASK_MAX) {
    return APP_EINVAL;
  }

  return lan9645_bum_write_verify(
    core,
    &LAN9645_REG_ANA_PGID,
    (uint32_t)pgid,
    0u,
    LAN9645_ANA_PGID_PGID_SET((uint32_t)member_mask),
    pgid_mask
  );
}

/**
 * @brief Applies deterministic BUM flooding baseline.
 */
app_status_t lan9645_bum_apply_baseline(lan9645_core_t *core)
{
  app_status_t st = lan9645_bum_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (s_bum_initialized == 0u) {
    (void)lan9645_bum_init();
  }

  st = lan9645_bum_set_pgid_mask(core, LAN9645_BUM_PGID_MC, LAN9645_BUM_FRONT_PORT_MASK);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_bum_set_pgid_mask(core, LAN9645_BUM_PGID_UC, LAN9645_BUM_FRONT_PORT_MASK);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_bum_set_pgid_mask(core, LAN9645_BUM_PGID_BC, LAN9645_BUM_FRONT_PORT_MASK);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_bum_set_pgid_mask(core, LAN9645_BUM_PGID_MCIPV4, 0u);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_bum_set_pgid_mask(core, LAN9645_BUM_PGID_MCIPV6, 0u);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_bum_set_ipmc_cfg(
    core,
    LAN9645_BUM_PGID_MC,
    LAN9645_BUM_PGID_MCIPV4,
    LAN9645_BUM_PGID_MC,
    LAN9645_BUM_PGID_MCIPV6
  );
  if (st != APP_OK) {
    return st;
  }

  for (uint32_t prio = 0u; prio < LAN9645_BUM_PRIO_COUNT; ++prio) {
    st = lan9645_bum_set_flood_cfg(
      core,
      (uint8_t)prio,
      LAN9645_BUM_PGID_UC,
      LAN9645_BUM_PGID_BC,
      LAN9645_BUM_PGID_MC
    );
    if (st != APP_OK) {
      return st;
    }
  }

  return APP_OK;
}
