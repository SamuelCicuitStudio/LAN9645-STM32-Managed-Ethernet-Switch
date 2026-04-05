#ifndef LAN9645_CBS_H
#define LAN9645_CBS_H

/**
 * @file lan9645_cbs.h
 * @brief Baseline CBS (credit-based shaper) helpers for LAN9645 managed mode.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "app_common_types.h"
#include "lan9645/lan9645_core.h"

/**
 * @brief Initializes CBS baseline service state.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_cbs_init(void);

/** @brief Number of per-port queue scheduler elements covered by CBS helpers. */
#define LAN9645_CBS_QUEUE_COUNT     (8u)
/** @brief Maximum encoded `QSYS_SE_CFG.SE_FRM_MODE` field value for CBS queue entries. */
#define LAN9645_CBS_FRM_MODE_MAX    (3u)
/** @brief Maximum encoded `QSYS_CIR_CFG.CIR_RATE` field value for CBS queue entries. */
#define LAN9645_CBS_CIR_RATE_MAX    (0x7FFFu)
/** @brief Maximum encoded `QSYS_CIR_CFG.CIR_BURST` field value for CBS queue entries. */
#define LAN9645_CBS_CIR_BURST_MAX   (0x3Fu)

/**
 * @brief Snapshot of one CBS queue scheduler-element configuration.
 */
typedef struct {
  uint8_t port;       /**< Port used to resolve queue scheduler element index. */
  uint8_t queue;      /**< Queue index (`0..7`). */
  uint8_t avb_enable; /**< `SE_AVB_ENA` bit (`0|1`). */
  uint8_t frm_mode;   /**< `SE_FRM_MODE` field (`0..3`). */
  uint16_t cir_rate;  /**< `CIR_RATE` field (`0..0x7FFF`). */
  uint8_t cir_burst;  /**< `CIR_BURST` field (`0..0x3F`). */
} lan9645_cbs_cfg_t;

/**
 * @brief Applies deterministic CBS baseline with shaping disabled on managed queues.
 *
 * Baseline policy:
 * - for queue scheduler elements on ports `0/1/8`, keep AVB path enabled
 * - force frame mode to disabled (`SE_FRM_MODE=0`)
 * - clear `QSYS_CIR_CFG` rate/burst values
 *
 * @param[in,out] core Initialized LAN9645 core context.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_cbs_apply_baseline(lan9645_core_t *core);

/**
 * @brief Reads one queue scheduler-element CBS configuration.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] port Port index.
 * @param[in] queue Queue index (`0..7`).
 * @param[out] out Decoded CBS queue snapshot.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_cbs_get_queue_cfg(
  lan9645_core_t *core,
  uint8_t port,
  uint8_t queue,
  lan9645_cbs_cfg_t *out
);

/**
 * @brief Writes one queue scheduler-element CBS configuration and verifies readback.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] port Port index.
 * @param[in] queue Queue index (`0..7`).
 * @param[in] avb_enable `SE_AVB_ENA` value (`0|1`).
 * @param[in] frm_mode `SE_FRM_MODE` value (`0..3`).
 * @param[in] cir_rate `CIR_RATE` value (`0..0x7FFF`).
 * @param[in] cir_burst `CIR_BURST` value (`0..0x3F`).
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_cbs_set_queue_cfg(
  lan9645_core_t *core,
  uint8_t port,
  uint8_t queue,
  uint8_t avb_enable,
  uint8_t frm_mode,
  uint16_t cir_rate,
  uint8_t cir_burst
);

#ifdef __cplusplus
}
#endif

#endif /* LAN9645_CBS_H */
