#ifndef LAN9645_FP_H
#define LAN9645_FP_H

/**
 * @file lan9645_fp.h
 * @brief Frame-preemption (FP/MM) helpers for LAN9645 managed mode.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "app_common_types.h"
#include "lan9645/lan9645_core.h"

/** @brief Maximum `QSYS_PREEMPT_CFG.P_QUEUES` bit-mask value. */
#define LAN9645_FP_PREEMPT_QUEUE_MASK_MAX  (0xFFu)
/** @brief Maximum `QSYS_PREEMPT_CFG.STRICT_IPG` field value. */
#define LAN9645_FP_STRICT_IPG_MAX          (3u)
/** @brief Maximum `DEV_VERIF_CONFIG.PRM_VERIFY_TIME` field value. */
#define LAN9645_FP_VERIFY_TIME_MAX         (255u)
/** @brief Maximum `DEV_VERIF_CONFIG.VERIF_TIMER_UNITS` field value. */
#define LAN9645_FP_VERIFY_UNITS_MAX        (3u)
/** @brief Maximum `SYS_FRONT_PORT_MODE.ADD_FRAG_SIZE` field value. */
#define LAN9645_FP_ADD_FRAG_SIZE_MAX       (3u)

/**
 * @brief One per-port FP/MM control snapshot.
 */
typedef struct {
  uint8_t port;                 /**< DEV/QSYS/SYS front-port index. */
  uint8_t mm_rx_enable;         /**< `DEV_ENABLE_CONFIG.MM_RX_ENA` */
  uint8_t mm_tx_enable;         /**< `DEV_ENABLE_CONFIG.MM_TX_ENA` */
  uint8_t keep_s_after_d;       /**< `DEV_ENABLE_CONFIG.KEEP_S_AFTER_D` */
  uint8_t verify_disable;       /**< `DEV_VERIF_CONFIG.PRM_VERIFY_DIS` */
  uint8_t verify_time;          /**< `DEV_VERIF_CONFIG.PRM_VERIFY_TIME` */
  uint8_t verify_timer_units;   /**< `DEV_VERIF_CONFIG.VERIF_TIMER_UNITS` */
  uint8_t add_frag_size;        /**< `SYS_FRONT_PORT_MODE.ADD_FRAG_SIZE` */
  uint8_t preempt_queue_mask;   /**< `QSYS_PREEMPT_CFG.P_QUEUES` */
  uint8_t strict_ipg;           /**< `QSYS_PREEMPT_CFG.STRICT_IPG` */
} lan9645_fp_port_cfg_t;

/**
 * @brief Initializes FP baseline service state.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_fp_init(void);

/**
 * @brief Reads one per-port FP/MM configuration snapshot.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] port DEV/QSYS/SYS front-port index.
 * @param[out] out Destination FP snapshot.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_fp_get_port_cfg(
  lan9645_core_t *core,
  uint8_t port,
  lan9645_fp_port_cfg_t *out
);

/**
 * @brief Writes one per-port FP/MM configuration.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] port DEV/QSYS/SYS front-port index.
 * @param[in] cfg Configuration values to program.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_fp_set_port_cfg(
  lan9645_core_t *core,
  uint8_t port,
  const lan9645_fp_port_cfg_t *cfg
);

/**
 * @brief Applies deterministic FP baseline on managed ports `0/1/8`.
 *
 * Baseline policy (Linux-aligned safe default):
 * - enable MAC-merge RX/TX and clear `KEEP_S_AFTER_D`
 * - disable verification state machine (`PRM_VERIFY_DIS=1`)
 * - clear preemptable queue mask (`P_QUEUES=0`)
 * - clear strict IPG profile and additional fragment size
 *
 * @param[in,out] core Initialized LAN9645 core context.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_fp_apply_baseline(lan9645_core_t *core);

#ifdef __cplusplus
}
#endif

#endif /* LAN9645_FP_H */
