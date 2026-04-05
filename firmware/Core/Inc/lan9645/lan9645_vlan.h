#ifndef LAN9645_VLAN_H
#define LAN9645_VLAN_H

/**
 * @file lan9645_vlan.h
 * @brief Minimal VLAN baseline service for LAN9645 managed mode.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "app_common_types.h"
#include "lan9645/lan9645_core.h"

/** @brief Highest VLAN identifier supported by the PGID-index baseline path. */
#define LAN9645_VLAN_VID_MAX            (89u)
/** @brief Bit mask of valid egress-member ports in PGID entries. */
#define LAN9645_VLAN_MEMBER_MASK_ALL    (0x03FFu)

/**
 * @brief Snapshot of one baseline VLAN entry state.
 */
typedef struct {
  uint8_t configured;    /**< Set when this VID has been configured through @ref lan9645_vlan_set. */
  uint8_t obey_vlan;     /**< Hardware state of `ANA_PGID_CFG.OBEY_VLAN` for this VID. */
  uint16_t member_mask;  /**< Hardware state of `ANA_PGID.PGID` for this VID. */
  uint16_t untag_mask;   /**< Firmware-tracked untag policy for this VID. */
} lan9645_vlan_entry_t;

/**
 * @brief Initializes the VLAN baseline cache.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_vlan_init(void);

/**
 * @brief Applies one baseline VLAN entry and verifies register readback.
 *
 * This baseline implementation maps `vid` directly to `ANA_PGID(vid)` and
 * `ANA_PGID_CFG(vid)`, therefore the supported range is `0..89`.
 *
 * @param[in,out] core LAN9645 core context.
 * @param[in] vid VLAN identifier in baseline range.
 * @param[in] member_mask Egress member mask written to `ANA_PGID.PGID`.
 * @param[in] untag_mask Firmware-tracked untag policy (must be subset of member mask).
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_vlan_set(
  lan9645_core_t *core,
  uint16_t vid,
  uint16_t member_mask,
  uint16_t untag_mask
);

/**
 * @brief Reads one baseline VLAN entry snapshot.
 *
 * @param[in,out] core LAN9645 core context.
 * @param[in] vid VLAN identifier in baseline range.
 * @param[out] out Destination VLAN entry snapshot.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_vlan_get(lan9645_core_t *core, uint16_t vid, lan9645_vlan_entry_t *out);

#ifdef __cplusplus
}
#endif

#endif /* LAN9645_VLAN_H */
