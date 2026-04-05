#include "lan9645/lan9645_mdb.h"

/**
 * @file lan9645_mdb.c
 * @brief MDB (multicast database) configuration aligned with Linux LAN9645 behavior.
 */

#include "lan9645/lan9645_reg.h"
#include "lan9645/lan9645_regs.h"

#define LAN9645_MDB_PGID_AGGR     (64u)
#define LAN9645_MDB_PGID_MCIPV4   (LAN9645_MDB_PGID_AGGR - 2u)
#define LAN9645_MDB_PGID_MCIPV6   (LAN9645_MDB_PGID_AGGR - 1u)

static uint8_t s_mdb_initialized = 0u;

/**
 * @brief Validates that MDB operations can access a ready core/register layer.
 */
static app_status_t lan9645_mdb_validate_core(const lan9645_core_t *core)
{
  if ((core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }

  return APP_OK;
}

/**
 * @brief Validates one PGID index used by MDB runtime APIs.
 */
static app_status_t lan9645_mdb_validate_pgid_index(uint8_t pgid_index)
{
  if (pgid_index > LAN9645_MDB_PGID_INDEX_MAX) {
    return APP_EINVAL;
  }

  return APP_OK;
}

/**
 * @brief Writes one descriptor register and verifies masked readback.
 */
static app_status_t lan9645_mdb_write_verify(
  lan9645_core_t *core,
  const lan9645_reg_desc_t *desc,
  uint32_t group_instance,
  uint32_t reg_instance,
  uint32_t reg_value,
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
    reg_value
  );
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_reg_read32_desc(
    core->reg,
    desc,
    0u,
    group_instance,
    reg_instance,
    &rd
  );
  if (st != APP_OK) {
    return st;
  }

  if ((rd & verify_mask) != (reg_value & verify_mask)) {
    return APP_EIO;
  }

  return APP_OK;
}

/**
 * @brief Applies "obey VLAN" policy on the full baseline PGID table.
 */
static app_status_t lan9645_mdb_apply_obey_vlan_all(lan9645_core_t *core)
{
  for (uint32_t idx = 0u; idx <= (uint32_t)LAN9645_MDB_PGID_INDEX_MAX; ++idx) {
    app_status_t st = lan9645_mdb_set_pgid_cfg(core, (uint8_t)idx, 0u, 0u, 1u);
    if (st != APP_OK) {
      return st;
    }
  }

  return APP_OK;
}

/**
 * @brief Applies multicast-snooping baseline by clearing IP multicast data PGIDs.
 */
static app_status_t lan9645_mdb_apply_snoop_default(lan9645_core_t *core)
{
  return lan9645_mdb_set_ipmc_cfg(core, 0u, 0u);
}

/**
 * @brief Initializes MDB service state.
 */
app_status_t lan9645_mdb_init(void)
{
  s_mdb_initialized = 1u;
  return APP_OK;
}

/**
 * @brief Reads one MDB PGID configuration entry.
 */
app_status_t lan9645_mdb_get_pgid_cfg(
  lan9645_core_t *core,
  uint8_t pgid_index,
  lan9645_mdb_pgid_cfg_t *out
)
{
  uint32_t rd = 0u;
  app_status_t st = lan9645_mdb_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_mdb_validate_pgid_index(pgid_index);
  if (st != APP_OK) {
    return st;
  }
  if (out == NULL) {
    return APP_EINVAL;
  }

  st = lan9645_reg_read32_desc(
    core->reg,
    &LAN9645_REG_ANA_PGID_CFG,
    0u,
    (uint32_t)pgid_index,
    0u,
    &rd
  );
  if (st != APP_OK) {
    return st;
  }

  out->pgid_index = pgid_index;
  out->san_ena = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_PGID_CFG_SAN_ENA, rd);
  out->cpuq_dst_pgid = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_PGID_CFG_CPUQ_DST_PGID, rd);
  out->obey_vlan = (uint8_t)LAN9645_FIELD_GET(LAN9645_ANA_PGID_CFG_OBEY_VLAN, rd);

  return APP_OK;
}

/**
 * @brief Writes one MDB PGID configuration entry with readback verification.
 */
app_status_t lan9645_mdb_set_pgid_cfg(
  lan9645_core_t *core,
  uint8_t pgid_index,
  uint8_t san_ena,
  uint8_t cpuq_dst_pgid,
  uint8_t obey_vlan
)
{
  const uint32_t mask =
    LAN9645_ANA_PGID_CFG_SAN_ENA |
    LAN9645_ANA_PGID_CFG_CPUQ_DST_PGID |
    LAN9645_ANA_PGID_CFG_OBEY_VLAN;
  const uint32_t value =
    (LAN9645_ANA_PGID_CFG_SAN_ENA * (uint32_t)(san_ena != 0u)) |
    LAN9645_ANA_PGID_CFG_CPUQ_DST_PGID_SET((uint32_t)cpuq_dst_pgid) |
    (LAN9645_ANA_PGID_CFG_OBEY_VLAN * (uint32_t)(obey_vlan != 0u));
  app_status_t st = lan9645_mdb_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_mdb_validate_pgid_index(pgid_index);
  if (st != APP_OK) {
    return st;
  }
  if ((san_ena > 1u) || (obey_vlan > 1u) || (cpuq_dst_pgid > LAN9645_MDB_CPUQ_DST_MAX)) {
    return APP_EINVAL;
  }

  return lan9645_mdb_write_verify(
    core,
    &LAN9645_REG_ANA_PGID_CFG,
    (uint32_t)pgid_index,
    0u,
    value,
    mask
  );
}

/**
 * @brief Reads one MDB destination member mask from `ANA_PGID`.
 */
app_status_t lan9645_mdb_get_pgid_mask(
  lan9645_core_t *core,
  uint8_t pgid_index,
  uint16_t *out_member_mask
)
{
  uint32_t rd = 0u;
  app_status_t st = lan9645_mdb_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_mdb_validate_pgid_index(pgid_index);
  if (st != APP_OK) {
    return st;
  }
  if (out_member_mask == NULL) {
    return APP_EINVAL;
  }

  st = lan9645_reg_read32_desc(
    core->reg,
    &LAN9645_REG_ANA_PGID,
    0u,
    (uint32_t)pgid_index,
    0u,
    &rd
  );
  if (st != APP_OK) {
    return st;
  }

  *out_member_mask = (uint16_t)LAN9645_FIELD_GET(LAN9645_ANA_PGID_PGID, rd);
  return APP_OK;
}

/**
 * @brief Writes one MDB destination member mask with readback verification.
 */
app_status_t lan9645_mdb_set_pgid_mask(
  lan9645_core_t *core,
  uint8_t pgid_index,
  uint16_t member_mask
)
{
  app_status_t st = lan9645_mdb_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_mdb_validate_pgid_index(pgid_index);
  if (st != APP_OK) {
    return st;
  }
  if (member_mask > LAN9645_MDB_MEMBER_MASK_MAX) {
    return APP_EINVAL;
  }

  return lan9645_mdb_write_verify(
    core,
    &LAN9645_REG_ANA_PGID,
    (uint32_t)pgid_index,
    0u,
    LAN9645_ANA_PGID_PGID_SET((uint32_t)member_mask),
    LAN9645_ANA_PGID_PGID
  );
}

/**
 * @brief Reads multicast destination masks used by MDB baseline policy.
 */
app_status_t lan9645_mdb_get_ipmc_cfg(lan9645_core_t *core, lan9645_mdb_ipmc_cfg_t *out)
{
  app_status_t st = lan9645_mdb_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (out == NULL) {
    return APP_EINVAL;
  }

  st = lan9645_mdb_get_pgid_mask(core, (uint8_t)LAN9645_MDB_PGID_MCIPV4, &out->mcipv4_mask);
  if (st != APP_OK) {
    return st;
  }

  return lan9645_mdb_get_pgid_mask(core, (uint8_t)LAN9645_MDB_PGID_MCIPV6, &out->mcipv6_mask);
}

/**
 * @brief Writes multicast destination masks used by MDB baseline policy.
 */
app_status_t lan9645_mdb_set_ipmc_cfg(
  lan9645_core_t *core,
  uint16_t mcipv4_mask,
  uint16_t mcipv6_mask
)
{
  app_status_t st = lan9645_mdb_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if ((mcipv4_mask > LAN9645_MDB_MEMBER_MASK_MAX) ||
      (mcipv6_mask > LAN9645_MDB_MEMBER_MASK_MAX)) {
    return APP_EINVAL;
  }

  st = lan9645_mdb_set_pgid_mask(core, (uint8_t)LAN9645_MDB_PGID_MCIPV4, mcipv4_mask);
  if (st != APP_OK) {
    return st;
  }

  return lan9645_mdb_set_pgid_mask(core, (uint8_t)LAN9645_MDB_PGID_MCIPV6, mcipv6_mask);
}

/**
 * @brief Applies deterministic MDB baseline.
 */
app_status_t lan9645_mdb_apply_baseline(lan9645_core_t *core)
{
  app_status_t st = lan9645_mdb_validate_core(core);
  if (st != APP_OK) {
    return st;
  }
  if (s_mdb_initialized == 0u) {
    (void)lan9645_mdb_init();
  }

  st = lan9645_mdb_apply_obey_vlan_all(core);
  if (st != APP_OK) {
    return st;
  }

  return lan9645_mdb_apply_snoop_default(core);
}
