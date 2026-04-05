#ifndef LAN9645_AFI_H
#define LAN9645_AFI_H

/**
 * @file lan9645_afi.h
 * @brief AFI-domain helpers for LAN9645 managed mode.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "app_common_types.h"
#include "lan9645/lan9645_core.h"

/** @brief Maximum boolean selector value used by AFI runtime APIs. */
#define LAN9645_AFI_BOOL_MAX (1u)

/**
 * @brief AFI runtime configuration snapshot.
 */
typedef struct {
  uint8_t afi_enable; /**< `AFI_MISC_CTRL.AFI_ENA` */
} lan9645_afi_cfg_t;

/**
 * @brief Initializes AFI service state.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_afi_init(void);

/**
 * @brief Reads AFI runtime configuration.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[out] out Destination AFI configuration snapshot.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_afi_get_cfg(lan9645_core_t *core, lan9645_afi_cfg_t *out);

/**
 * @brief Writes AFI runtime configuration with readback verification.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] cfg Source AFI configuration.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_afi_set_cfg(lan9645_core_t *core, const lan9645_afi_cfg_t *cfg);

/**
 * @brief Applies deterministic AFI baseline.
 *
 * Baseline policy:
 * - enable AFI globally (`AFI_MISC_CTRL.AFI_ENA=1`)
 *
 * @param[in,out] core Initialized LAN9645 core context.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_afi_apply_baseline(lan9645_core_t *core);

#ifdef __cplusplus
}
#endif

#endif /* LAN9645_AFI_H */
