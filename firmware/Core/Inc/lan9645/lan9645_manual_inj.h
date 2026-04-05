#ifndef LAN9645_MANUAL_INJ_H
#define LAN9645_MANUAL_INJ_H

/**
 * @file lan9645_manual_inj.h
 * @brief Manual-injection helpers for LAN9645 managed mode.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "app_common_types.h"
#include "lan9645/lan9645_core.h"

/** @brief Default dedicated injection port used by managed profile. */
#define LAN9645_MANUAL_INJ_DEFAULT_PORT        (9u)
/** @brief Maximum supported port index in this board profile. */
#define LAN9645_MANUAL_INJ_PORT_MAX            (9u)
/** @brief Maximum `INCL_INJ_HDR` selector value. */
#define LAN9645_MANUAL_INJ_INCL_INJ_HDR_MAX    (3u)

/**
 * @brief Manual-injection runtime configuration snapshot.
 */
typedef struct {
  uint8_t port;          /**< Target port index. */
  uint8_t incl_inj_hdr;  /**< `SYS_PORT_MODE.INCL_INJ_HDR` */
} lan9645_manual_inj_cfg_t;

/**
 * @brief Initializes manual-injection service state.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_manual_inj_init(void);

/**
 * @brief Reads manual-injection configuration for one port.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] port Port selector.
 * @param[out] out Destination manual-injection snapshot.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_manual_inj_get_cfg(
  lan9645_core_t *core,
  uint8_t port,
  lan9645_manual_inj_cfg_t *out
);

/**
 * @brief Writes manual-injection configuration with readback verification.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] cfg Source configuration.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_manual_inj_set_cfg(
  lan9645_core_t *core,
  const lan9645_manual_inj_cfg_t *cfg
);

/**
 * @brief Applies deterministic manual-injection baseline.
 *
 * Baseline policy:
 * - configure `SYS_PORT_MODE.INCL_INJ_HDR=1` on dedicated injection port `9`
 *
 * @param[in,out] core Initialized LAN9645 core context.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_manual_inj_apply_baseline(lan9645_core_t *core);

#ifdef __cplusplus
}
#endif

#endif /* LAN9645_MANUAL_INJ_H */
