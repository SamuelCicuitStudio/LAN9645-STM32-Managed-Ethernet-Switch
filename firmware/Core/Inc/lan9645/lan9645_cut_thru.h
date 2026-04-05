#ifndef LAN9645_CUT_THRU_H
#define LAN9645_CUT_THRU_H

/**
 * @file lan9645_cut_thru.h
 * @brief Baseline cut-through forwarding helpers for LAN9645 managed mode.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "app_common_types.h"
#include "lan9645/lan9645_core.h"

/**
 * @brief Initializes cut-through baseline service state.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_cut_thru_init(void);

/** @brief Maximum encoded cut-through speed selector value (`ANA_CUT_THRU_CFG.CUT_THRU_SPD`). */
#define LAN9645_CUT_THRU_SPD_MAX      (7u)
/** @brief Maximum traffic-class enable mask value (`ANA_CUT_THRU_CFG.CUT_THRU_ENA`). */
#define LAN9645_CUT_THRU_ENA_MASK_MAX (0xFFu)

/**
 * @brief Snapshot of one port cut-through configuration register.
 */
typedef struct {
  uint8_t port;               /**< Port instance index used for `ANA_CUT_THRU_CFG`. */
  uint8_t cut_thru_spd;       /**< Encoded speed selector (`CUT_THRU_SPD`, `0..7`). */
  uint8_t cut_thru_ena_mask;  /**< Traffic-class enable mask (`CUT_THRU_ENA`, `0..0xFF`). */
} lan9645_cut_thru_cfg_t;

/**
 * @brief Applies deterministic cut-through baseline with cut-through disabled.
 *
 * Baseline policy:
 * - clear `ANA_CUT_THRU_CFG.CUT_THRU_SPD` on managed ports `0/1/8`
 * - clear `ANA_CUT_THRU_CFG.CUT_THRU_ENA` traffic-class mask on managed ports `0/1/8`
 *
 * @param[in,out] core Initialized LAN9645 core context.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_cut_thru_apply_baseline(lan9645_core_t *core);

/**
 * @brief Reads one port cut-through configuration with field decode.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] port Port instance for `ANA_CUT_THRU_CFG`.
 * @param[out] out Decoded cut-through configuration snapshot.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_cut_thru_get_cfg(
  lan9645_core_t *core,
  uint8_t port,
  lan9645_cut_thru_cfg_t *out
);

/**
 * @brief Writes one port cut-through configuration and verifies readback.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] port Port instance for `ANA_CUT_THRU_CFG`.
 * @param[in] cut_thru_spd Encoded speed selector (`0..7`).
 * @param[in] cut_thru_ena_mask Traffic-class enable mask (`0..0xFF`).
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_cut_thru_set_cfg(
  lan9645_core_t *core,
  uint8_t port,
  uint8_t cut_thru_spd,
  uint8_t cut_thru_ena_mask
);

#ifdef __cplusplus
}
#endif

#endif /* LAN9645_CUT_THRU_H */
