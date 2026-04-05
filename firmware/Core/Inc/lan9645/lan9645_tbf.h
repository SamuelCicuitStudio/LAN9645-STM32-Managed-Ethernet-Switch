#ifndef LAN9645_TBF_H
#define LAN9645_TBF_H

/**
 * @file lan9645_tbf.h
 * @brief Baseline TBF (token bucket filter) helpers for LAN9645 managed mode.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "app_common_types.h"
#include "lan9645/lan9645_core.h"

/**
 * @brief Initializes TBF baseline service state.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_tbf_init(void);

/** @brief Number of per-port queue scheduler elements covered by TBF helpers. */
#define LAN9645_TBF_QUEUE_COUNT       (8u)
/** @brief Maximum encoded `QSYS_SE_CFG.SE_FRM_MODE` field value. */
#define LAN9645_TBF_FRM_MODE_MAX      (3u)
/** @brief Maximum encoded `QSYS_CIR_CFG.CIR_RATE` field value. */
#define LAN9645_TBF_CIR_RATE_MAX      (0x7FFFu)
/** @brief Maximum encoded `QSYS_CIR_CFG.CIR_BURST` field value. */
#define LAN9645_TBF_CIR_BURST_MAX     (0x3Fu)

/**
 * @brief Scope selector for TBF access helpers.
 */
typedef enum {
  LAN9645_TBF_SCOPE_QUEUE = 0, /**< Queue scheduler element (`port*8 + queue`). */
  LAN9645_TBF_SCOPE_PORT = 1   /**< Port scheduler element (`88 + port`). */
} lan9645_tbf_scope_t;

/**
 * @brief Snapshot of one TBF scheduler-element configuration.
 */
typedef struct {
  uint8_t port;           /**< Port used to resolve the scheduler-element index. */
  uint8_t queue;          /**< Queue index when scope is queue (`0..7`), else `0`. */
  uint8_t avb_enable;     /**< `SE_AVB_ENA` bit (`0|1`). */
  uint8_t frm_mode;       /**< `SE_FRM_MODE` field (`0..3`). */
  uint16_t cir_rate;      /**< `CIR_RATE` field (`0..0x7FFF`). */
  uint8_t cir_burst;      /**< `CIR_BURST` field (`0..0x3F`). */
  lan9645_tbf_scope_t scope; /**< Queue or port scheduler-element scope. */
} lan9645_tbf_cfg_t;

/**
 * @brief Applies deterministic TBF baseline with shaping disabled.
 *
 * Baseline policy:
 * - disable TBF mode for queue and port scheduler elements of managed ports `0/1/8`
 * - clear CIR rate and burst on corresponding `QSYS_CIR_CFG` entries
 *
 * @param[in,out] core Initialized LAN9645 core context.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_tbf_apply_baseline(lan9645_core_t *core);

/**
 * @brief Reads one queue/port scheduler-element TBF configuration.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] port Port index.
 * @param[in] scope Queue or port scheduler-element scope.
 * @param[in] queue Queue index (`0..7`) when scope is queue, ignored for port scope.
 * @param[out] out Decoded TBF snapshot.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_tbf_get_cfg(
  lan9645_core_t *core,
  uint8_t port,
  lan9645_tbf_scope_t scope,
  uint8_t queue,
  lan9645_tbf_cfg_t *out
);

/**
 * @brief Writes one queue/port scheduler-element TBF configuration and verifies readback.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] port Port index.
 * @param[in] scope Queue or port scheduler-element scope.
 * @param[in] queue Queue index (`0..7`) when scope is queue, ignored for port scope.
 * @param[in] avb_enable `SE_AVB_ENA` value (`0|1`).
 * @param[in] frm_mode `SE_FRM_MODE` value (`0..3`).
 * @param[in] cir_rate `CIR_RATE` value (`0..0x7FFF`).
 * @param[in] cir_burst `CIR_BURST` value (`0..0x3F`).
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_tbf_set_cfg(
  lan9645_core_t *core,
  uint8_t port,
  lan9645_tbf_scope_t scope,
  uint8_t queue,
  uint8_t avb_enable,
  uint8_t frm_mode,
  uint16_t cir_rate,
  uint8_t cir_burst
);

#ifdef __cplusplus
}
#endif

#endif /* LAN9645_TBF_H */
