#ifndef LAN9645_POLICE_H
#define LAN9645_POLICE_H

/**
 * @file lan9645_police.h
 * @brief Baseline policer-domain control helpers for LAN9645 managed mode.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "app_common_types.h"
#include "lan9645/lan9645_core.h"

/**
 * @brief Initializes policer baseline service state.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_police_init(void);

/** @brief Number of port-policer indices used in the managed baseline (`0..9`). */
#define LAN9645_POLICE_PORT_POL_COUNT   (10u)
/** @brief Maximum `ANA_POL_CFG.POL_ORDER` value (`9-bit`). */
#define LAN9645_POLICE_ORDER_MAX        (0x1FFu)
/** @brief Maximum `ANA_POL_CFG.QUEUE_POL_ENA` queue-mask value (`8-bit`). */
#define LAN9645_POLICE_QUEUE_MASK_MAX   (0xFFu)
/** @brief Maximum `ANA_POL_MODE.IPG_SIZE` value (`5-bit`). */
#define LAN9645_POLICE_IPG_SIZE_MAX     (0x1Fu)
/** @brief Maximum `ANA_POL_MODE.FRM_MODE` value (`2-bit`). */
#define LAN9645_POLICE_FRM_MODE_MAX     (0x3u)
/** @brief Maximum `ANA_POL_PIR_STATE.PIR_LVL` value (`22-bit`). */
#define LAN9645_POLICE_PIR_LVL_MAX      (0x3FFFFFu)
/** @brief Maximum `ANA_POL_PIR_CFG.PIR_RATE` value (`15-bit`). */
#define LAN9645_POLICE_PIR_RATE_MAX     (0x7FFFu)
/** @brief Maximum `ANA_POL_PIR_CFG.PIR_BURST` value (`6-bit`). */
#define LAN9645_POLICE_PIR_BURST_MAX    (0x3Fu)

/**
 * @brief Snapshot of one per-port policer control register.
 */
typedef struct {
  uint8_t port;              /**< Port instance for `ANA_POL_CFG(port)`. */
  uint8_t cpu_redir_8021;    /**< `POL_CPU_REDIR_8021` (`0|1`). */
  uint8_t cpu_redir_ip;      /**< `POL_CPU_REDIR_IP` (`0|1`). */
  uint8_t port_pol_enable;   /**< `PORT_POL_ENA` (`0|1`). */
  uint8_t queue_pol_enable;  /**< `QUEUE_POL_ENA` mask (`0..0xFF`). */
  uint16_t pol_order;        /**< `POL_ORDER` (`0..0x1FF`). */
} lan9645_police_port_cfg_t;

/**
 * @brief Snapshot of one port-policer index (`ANA_POL_MODE/PIR_*`).
 */
typedef struct {
  uint8_t pol_idx;           /**< Policer index (`0..9`). */
  uint8_t drop_on_yellow;    /**< `DROP_ON_YELLOW_ENA` (`0|1`). */
  uint8_t mark_all_red;      /**< `MARK_ALL_FRMS_RED_ENA` (`0|1`). */
  uint8_t ipg_size;          /**< `IPG_SIZE` (`0..31`). */
  uint8_t frm_mode;          /**< `FRM_MODE` (`0..3`). */
  uint8_t dlb_coupled;       /**< `DLB_COUPLED` (`0|1`). */
  uint8_t cir_enable;        /**< `CIR_ENA` (`0|1`). */
  uint8_t overshoot_enable;  /**< `OVERSHOOT_ENA` (`0|1`). */
  uint32_t pir_level;        /**< `PIR_LVL` (`0..0x3FFFFF`). */
  uint16_t pir_rate;         /**< `PIR_RATE` (`0..0x7FFF`). */
  uint8_t pir_burst;         /**< `PIR_BURST` (`0..0x3F`). */
} lan9645_police_index_cfg_t;

/**
 * @brief Applies deterministic policer baseline configuration.
 *
 * Baseline policy:
 * - configure per-port policer order and CPU redirect policy
 * - disable port and queue policers on managed ports
 * - reset port policer indices to disabled mode and default token state
 *
 * @param[in,out] core Initialized LAN9645 core context.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_police_apply_baseline(lan9645_core_t *core);

/**
 * @brief Reads one per-port policer configuration (`ANA_POL_CFG`).
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] port Port instance.
 * @param[out] out Decoded per-port policer snapshot.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_police_get_port_cfg(
  lan9645_core_t *core,
  uint8_t port,
  lan9645_police_port_cfg_t *out
);

/**
 * @brief Writes one per-port policer configuration (`ANA_POL_CFG`) with readback verification.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] port Port instance.
 * @param[in] cpu_redir_8021 `POL_CPU_REDIR_8021` (`0|1`).
 * @param[in] cpu_redir_ip `POL_CPU_REDIR_IP` (`0|1`).
 * @param[in] port_pol_enable `PORT_POL_ENA` (`0|1`).
 * @param[in] queue_pol_enable `QUEUE_POL_ENA` mask (`0..0xFF`).
 * @param[in] pol_order `POL_ORDER` (`0..0x1FF`).
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_police_set_port_cfg(
  lan9645_core_t *core,
  uint8_t port,
  uint8_t cpu_redir_8021,
  uint8_t cpu_redir_ip,
  uint8_t port_pol_enable,
  uint8_t queue_pol_enable,
  uint16_t pol_order
);

/**
 * @brief Reads one policer index configuration (`ANA_POL_MODE/PIR_*`).
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] pol_idx Policer index (`0..9`).
 * @param[out] out Decoded policer-index snapshot.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_police_get_index_cfg(
  lan9645_core_t *core,
  uint8_t pol_idx,
  lan9645_police_index_cfg_t *out
);

/**
 * @brief Writes one policer index configuration (`ANA_POL_MODE/PIR_*`) with readback verification.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] pol_idx Policer index (`0..9`).
 * @param[in] drop_on_yellow `DROP_ON_YELLOW_ENA` (`0|1`).
 * @param[in] mark_all_red `MARK_ALL_FRMS_RED_ENA` (`0|1`).
 * @param[in] ipg_size `IPG_SIZE` (`0..31`).
 * @param[in] frm_mode `FRM_MODE` (`0..3`).
 * @param[in] dlb_coupled `DLB_COUPLED` (`0|1`).
 * @param[in] cir_enable `CIR_ENA` (`0|1`).
 * @param[in] overshoot_enable `OVERSHOOT_ENA` (`0|1`).
 * @param[in] pir_level `PIR_LVL` (`0..0x3FFFFF`).
 * @param[in] pir_rate `PIR_RATE` (`0..0x7FFF`).
 * @param[in] pir_burst `PIR_BURST` (`0..0x3F`).
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_police_set_index_cfg(
  lan9645_core_t *core,
  uint8_t pol_idx,
  uint8_t drop_on_yellow,
  uint8_t mark_all_red,
  uint8_t ipg_size,
  uint8_t frm_mode,
  uint8_t dlb_coupled,
  uint8_t cir_enable,
  uint8_t overshoot_enable,
  uint32_t pir_level,
  uint16_t pir_rate,
  uint8_t pir_burst
);

#ifdef __cplusplus
}
#endif

#endif /* LAN9645_POLICE_H */
