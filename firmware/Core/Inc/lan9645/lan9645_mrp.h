#ifndef LAN9645_MRP_H
#define LAN9645_MRP_H

/**
 * @file lan9645_mrp.h
 * @brief Baseline MRP-domain helpers for LAN9645 managed mode.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "app_common_types.h"
#include "lan9645/lan9645_core.h"

/** @brief Maximum destination mask width for `ANA_PGID.PGID` field. */
#define LAN9645_MRP_MEMBER_MASK_MAX (0x3FFu)

/**
 * @brief MRP runtime configuration snapshot.
 */
typedef struct {
  uint16_t pgid_mrp_mask; /**< `ANA_PGID(PGID_MRP).PGID` destination mask. */
} lan9645_mrp_cfg_t;

/**
 * @brief Initializes MRP baseline service state.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_mrp_init(void);

/**
 * @brief Reads MRP runtime configuration.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[out] out Destination configuration snapshot.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_mrp_get_cfg(lan9645_core_t *core, lan9645_mrp_cfg_t *out);

/**
 * @brief Writes MRP runtime configuration with readback verification.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] cfg Source configuration.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_mrp_set_cfg(lan9645_core_t *core, const lan9645_mrp_cfg_t *cfg);

/**
 * @brief Applies deterministic MRP baseline.
 *
 * Baseline policy:
 * - reserve `PGID_MRP` as a drop/blackhole destination by forcing its
 *   destination port mask to `0`
 *
 * @param[in,out] core Initialized LAN9645 core context.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_mrp_apply_baseline(lan9645_core_t *core);

#ifdef __cplusplus
}
#endif

#endif /* LAN9645_MRP_H */
