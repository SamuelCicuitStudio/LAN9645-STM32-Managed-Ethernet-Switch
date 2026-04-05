#ifndef LAN9645_MIRROR_H
#define LAN9645_MIRROR_H

/**
 * @file lan9645_mirror.h
 * @brief Mirror control helpers for LAN9645 managed mode.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "app_common_types.h"
#include "lan9645/lan9645_core.h"

/** @brief Maximum valid mirror bitmap mask (`ports 0..9`). */
#define LAN9645_MIRROR_PORT_MASK_MAX (0x3FFu)

/**
 * @brief Initializes mirror service state.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_mirror_init(void);

/**
 * @brief Reads global mirror destination masks.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[out] ingress_mask Destination for `ANA_MIRRORPORTS`.
 * @param[out] egress_mask Destination for `ANA_EMIRRORPORTS`.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_mirror_get_global_cfg(
  lan9645_core_t *core,
  uint16_t *ingress_mask,
  uint16_t *egress_mask
);

/**
 * @brief Writes global mirror destination masks with readback verification.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] ingress_mask New ingress mirror destination mask.
 * @param[in] egress_mask New egress mirror destination mask.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_mirror_set_global_cfg(
  lan9645_core_t *core,
  uint16_t ingress_mask,
  uint16_t egress_mask
);

/**
 * @brief Reads per-port ingress mirror-source enable state.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] port Source port index.
 * @param[out] enable Destination state (`0` or `1`).
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_mirror_get_src_port(
  lan9645_core_t *core,
  uint8_t port,
  uint8_t *enable
);

/**
 * @brief Writes per-port ingress mirror-source enable state with readback verification.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] port Source port index.
 * @param[in] enable New source enable (`0` or `1`).
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_mirror_set_src_port(
  lan9645_core_t *core,
  uint8_t port,
  uint8_t enable
);

/**
 * @brief Applies deterministic mirror baseline.
 *
 * Baseline policy is safe-off:
 * - clear `ANA_MIRRORPORTS`
 * - clear `ANA_EMIRRORPORTS`
 * - clear `ANA_PORT_CFG.SRC_MIRROR_ENA` on managed ports
 *
 * @param[in,out] core Initialized LAN9645 core context.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_mirror_apply_baseline(lan9645_core_t *core);

#ifdef __cplusplus
}
#endif

#endif /* LAN9645_MIRROR_H */
