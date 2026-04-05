#ifndef LAN9645_MDB_H
#define LAN9645_MDB_H

/**
 * @file lan9645_mdb.h
 * @brief Baseline MDB (multicast database) control helpers.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "app_common_types.h"
#include "lan9645/lan9645_core.h"

/** @brief Maximum PGID index covered by MDB baseline policy (`0..88`). */
#define LAN9645_MDB_PGID_INDEX_MAX   (88u)
/** @brief Maximum destination member mask width for `ANA_PGID.PGID`. */
#define LAN9645_MDB_MEMBER_MASK_MAX  (0x3FFu)
/** @brief Maximum queue destination selector in `ANA_PGID_CFG.CPUQ_DST_PGID`. */
#define LAN9645_MDB_CPUQ_DST_MAX     (7u)

/**
 * @brief One MDB PGID configuration snapshot.
 */
typedef struct {
  uint8_t pgid_index;     /**< PGID selector (`0..88`). */
  uint8_t san_ena;        /**< `ANA_PGID_CFG.SAN_ENA` */
  uint8_t cpuq_dst_pgid;  /**< `ANA_PGID_CFG.CPUQ_DST_PGID` */
  uint8_t obey_vlan;      /**< `ANA_PGID_CFG.OBEY_VLAN` */
} lan9645_mdb_pgid_cfg_t;

/**
 * @brief One MDB IP multicast destination snapshot.
 */
typedef struct {
  uint16_t mcipv4_mask;  /**< `ANA_PGID(PGID_MCIPV4).PGID` destination mask. */
  uint16_t mcipv6_mask;  /**< `ANA_PGID(PGID_MCIPV6).PGID` destination mask. */
} lan9645_mdb_ipmc_cfg_t;

/**
 * @brief Initializes MDB baseline service state.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_mdb_init(void);

/**
 * @brief Reads one MDB PGID configuration entry.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] pgid_index PGID selector (`0..88`).
 * @param[out] out Destination configuration snapshot.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_mdb_get_pgid_cfg(
  lan9645_core_t *core,
  uint8_t pgid_index,
  lan9645_mdb_pgid_cfg_t *out
);

/**
 * @brief Writes one MDB PGID configuration entry with readback verification.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] pgid_index PGID selector (`0..88`).
 * @param[in] san_ena `ANA_PGID_CFG.SAN_ENA` value (`0|1`).
 * @param[in] cpuq_dst_pgid `ANA_PGID_CFG.CPUQ_DST_PGID` value (`0..7`).
 * @param[in] obey_vlan `ANA_PGID_CFG.OBEY_VLAN` value (`0|1`).
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_mdb_set_pgid_cfg(
  lan9645_core_t *core,
  uint8_t pgid_index,
  uint8_t san_ena,
  uint8_t cpuq_dst_pgid,
  uint8_t obey_vlan
);

/**
 * @brief Reads one MDB destination member mask from `ANA_PGID`.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] pgid_index PGID selector (`0..88`).
 * @param[out] out_member_mask Destination member mask.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_mdb_get_pgid_mask(
  lan9645_core_t *core,
  uint8_t pgid_index,
  uint16_t *out_member_mask
);

/**
 * @brief Writes one MDB destination member mask with readback verification.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] pgid_index PGID selector (`0..88`).
 * @param[in] member_mask Destination member mask (`0x000..0x3FF`).
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_mdb_set_pgid_mask(
  lan9645_core_t *core,
  uint8_t pgid_index,
  uint16_t member_mask
);

/**
 * @brief Reads multicast destination masks used by MDB baseline policy.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[out] out Destination multicast configuration.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_mdb_get_ipmc_cfg(lan9645_core_t *core, lan9645_mdb_ipmc_cfg_t *out);

/**
 * @brief Writes multicast destination masks used by MDB baseline policy.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] mcipv4_mask `PGID_MCIPV4` destination mask (`0x000..0x3FF`).
 * @param[in] mcipv6_mask `PGID_MCIPV6` destination mask (`0x000..0x3FF`).
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_mdb_set_ipmc_cfg(
  lan9645_core_t *core,
  uint16_t mcipv4_mask,
  uint16_t mcipv6_mask
);

/**
 * @brief Applies deterministic MDB baseline for managed mode.
 *
 * Baseline policy:
 * - force all PGID entries (`0..88`) to obey VLAN on forwarding lookups
 * - keep IPv4/IPv6 multicast data PGIDs disabled (`MCIPV4`, `MCIPV6`)
 *   until explicit multicast membership programming is added
 *
 * @param[in,out] core Initialized LAN9645 core context.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_mdb_apply_baseline(lan9645_core_t *core);

#ifdef __cplusplus
}
#endif

#endif /* LAN9645_MDB_H */
