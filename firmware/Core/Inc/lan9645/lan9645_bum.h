#ifndef LAN9645_BUM_H
#define LAN9645_BUM_H

/**
 * @file lan9645_bum.h
 * @brief BUM (broadcast/unknown/multicast flooding) control helpers.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "app_common_types.h"
#include "lan9645/lan9645_core.h"

/** @brief Number of flooding priority entries in `ANA_FLOODING`. */
#define LAN9645_BUM_PRIO_COUNT        (8u)
/** @brief Maximum PGID value encoded in flooding fields. */
#define LAN9645_BUM_PGID_MAX          (63u)
/** @brief Maximum destination member mask width for `ANA_PGID.PGID`. */
#define LAN9645_BUM_MEMBER_MASK_MAX   (0x3FFu)

/**
 * @brief One flooding-priority configuration snapshot.
 */
typedef struct {
  uint8_t priority;  /**< Flooding priority selector (`0..7`). */
  uint8_t pgid_uc;   /**< Unknown-unicast destination PGID. */
  uint8_t pgid_bc;   /**< Broadcast destination PGID. */
  uint8_t pgid_mc;   /**< Unknown-multicast destination PGID. */
} lan9645_bum_flood_cfg_t;

/**
 * @brief One IPMC flooding configuration snapshot.
 */
typedef struct {
  uint8_t pgid_mc4_ctrl; /**< IPv4 control multicast PGID. */
  uint8_t pgid_mc4_data; /**< IPv4 data multicast PGID. */
  uint8_t pgid_mc6_ctrl; /**< IPv6 control multicast PGID. */
  uint8_t pgid_mc6_data; /**< IPv6 data multicast PGID. */
} lan9645_bum_ipmc_cfg_t;

/**
 * @brief Initializes BUM baseline service state.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_bum_init(void);

/**
 * @brief Reads one flooding-priority entry.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] priority Priority selector (`0..7`).
 * @param[out] out Destination flooding configuration.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_bum_get_flood_cfg(
  lan9645_core_t *core,
  uint8_t priority,
  lan9645_bum_flood_cfg_t *out
);

/**
 * @brief Writes one flooding-priority entry with readback verification.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] priority Priority selector (`0..7`).
 * @param[in] pgid_uc Unknown-unicast destination PGID.
 * @param[in] pgid_bc Broadcast destination PGID.
 * @param[in] pgid_mc Unknown-multicast destination PGID.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_bum_set_flood_cfg(
  lan9645_core_t *core,
  uint8_t priority,
  uint8_t pgid_uc,
  uint8_t pgid_bc,
  uint8_t pgid_mc
);

/**
 * @brief Reads IPMC flooding configuration.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[out] out Destination IPMC configuration.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_bum_get_ipmc_cfg(lan9645_core_t *core, lan9645_bum_ipmc_cfg_t *out);

/**
 * @brief Writes IPMC flooding configuration with readback verification.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] pgid_mc4_ctrl IPv4 control multicast PGID.
 * @param[in] pgid_mc4_data IPv4 data multicast PGID.
 * @param[in] pgid_mc6_ctrl IPv6 control multicast PGID.
 * @param[in] pgid_mc6_data IPv6 data multicast PGID.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_bum_set_ipmc_cfg(
  lan9645_core_t *core,
  uint8_t pgid_mc4_ctrl,
  uint8_t pgid_mc4_data,
  uint8_t pgid_mc6_ctrl,
  uint8_t pgid_mc6_data
);

/**
 * @brief Reads one `ANA_PGID` destination member mask.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] pgid PGID index (`0..63` for this API scope).
 * @param[out] member_mask Destination member mask.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_bum_get_pgid_mask(
  lan9645_core_t *core,
  uint8_t pgid,
  uint16_t *member_mask
);

/**
 * @brief Writes one `ANA_PGID` destination member mask with readback verification.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] pgid PGID index (`0..63` for this API scope).
 * @param[in] member_mask New destination member mask.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_bum_set_pgid_mask(
  lan9645_core_t *core,
  uint8_t pgid,
  uint16_t member_mask
);

/**
 * @brief Applies deterministic flooding baseline for managed mode.
 *
 * Baseline policy:
 * - reserve flood PGID roles according to Linux LAN9645 model
 * - route UC/BC/MC flood classes through reserved PGID indices
 * - keep IPMC data PGIDs disabled (snooping baseline)
 * - use managed front-port mask (`0/1/8`) for UC/BC/MC flood masks
 *
 * @param[in,out] core Initialized LAN9645 core context.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_bum_apply_baseline(lan9645_core_t *core);

#ifdef __cplusplus
}
#endif

#endif /* LAN9645_BUM_H */
