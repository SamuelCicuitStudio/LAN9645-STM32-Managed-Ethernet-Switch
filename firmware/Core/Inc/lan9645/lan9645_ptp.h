#ifndef LAN9645_PTP_H
#define LAN9645_PTP_H

/**
 * @file lan9645_ptp.h
 * @brief Baseline PTP-domain helpers for LAN9645 managed mode.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "app_common_types.h"
#include "lan9645/lan9645_core.h"

/** @brief Maximum boolean selector value used by PTP runtime APIs. */
#define LAN9645_PTP_BOOL_MAX (1u)

/**
 * @brief PTP runtime configuration snapshot.
 */
typedef struct {
  uint8_t ptp_enable;         /**< `1` when PTP is globally enabled. */
  uint8_t wait_for_ts_port0;  /**< `1` when port 0 waits for timestamp completion. */
  uint8_t wait_for_ts_port1;  /**< `1` when port 1 waits for timestamp completion. */
  uint8_t wait_for_ts_port8;  /**< `1` when port 8 waits for timestamp completion. */
} lan9645_ptp_cfg_t;

/**
 * @brief Initializes PTP baseline service state.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_ptp_init(void);

/**
 * @brief Reads PTP runtime configuration.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[out] out Destination configuration snapshot.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_ptp_get_cfg(lan9645_core_t *core, lan9645_ptp_cfg_t *out);

/**
 * @brief Writes PTP runtime configuration with readback verification.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] cfg Source configuration.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_ptp_set_cfg(lan9645_core_t *core, const lan9645_ptp_cfg_t *cfg);

/**
 * @brief Applies deterministic PTP baseline.
 *
 * Baseline policy:
 * - ensure PTP global feature-disable bit is cleared
 * - ensure front ports `0/1/8` keep `DONT_WAIT_FOR_TS=0`
 *
 * This keeps timestamp-dependent forwarding behavior deterministic for future
 * timestamping enablement without enabling advanced PTP runtime features yet.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_ptp_apply_baseline(lan9645_core_t *core);

#ifdef __cplusplus
}
#endif

#endif /* LAN9645_PTP_H */
