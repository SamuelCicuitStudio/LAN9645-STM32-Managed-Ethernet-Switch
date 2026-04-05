#ifndef LAN9645_HSR_PRP_H
#define LAN9645_HSR_PRP_H

/**
 * @file lan9645_hsr_prp.h
 * @brief Baseline HSR/PRP-domain helpers for LAN9645 managed mode.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "app_common_types.h"
#include "lan9645/lan9645_core.h"

/** @brief Maximum destination mask width for `ANA_PGID.PGID` field. */
#define LAN9645_HSR_PRP_PGID_MASK_MAX (0x3FFu)

/**
 * @brief HSR/PRP runtime configuration snapshot.
 */
typedef struct {
  uint16_t pgid_cpu_mask; /**< `ANA_PGID(PGID_CPU).PGID` destination mask. */
} lan9645_hsr_prp_cfg_t;

/**
 * @brief Initializes HSR/PRP baseline service state.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_hsr_prp_init(void);

/**
 * @brief Reads HSR/PRP runtime configuration.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[out] out Destination configuration snapshot.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_hsr_prp_get_cfg(lan9645_core_t *core, lan9645_hsr_prp_cfg_t *out);

/**
 * @brief Writes HSR/PRP runtime configuration with readback verification.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] cfg Source configuration.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_hsr_prp_set_cfg(lan9645_core_t *core, const lan9645_hsr_prp_cfg_t *cfg);

/**
 * @brief Applies deterministic HSR/PRP baseline.
 *
 * Baseline policy:
 * - map reserved `PGID_CPU` to CPU port only (`port 8`)
 * - keep this mapping readback-verified for later HSR/PRP supervision
 *   and control-plane integration steps
 *
 * @param[in,out] core Initialized LAN9645 core context.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_hsr_prp_apply_baseline(lan9645_core_t *core);

#ifdef __cplusplus
}
#endif

#endif /* LAN9645_HSR_PRP_H */
