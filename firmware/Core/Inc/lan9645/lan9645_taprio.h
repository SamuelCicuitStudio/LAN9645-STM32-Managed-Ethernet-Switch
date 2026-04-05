#ifndef LAN9645_TAPRIO_H
#define LAN9645_TAPRIO_H

/**
 * @file lan9645_taprio.h
 * @brief Baseline TAPRIO (IEEE 802.1Qbv) control helpers for LAN9645 managed mode.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "app_common_types.h"
#include "lan9645/lan9645_core.h"

/**
 * @brief Initializes TAPRIO baseline service state.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_taprio_init(void);

/** @brief Maximum encoded `TAS_STATEMACHINE_CFG.REVISIT_DLY` field value. */
#define LAN9645_TAPRIO_REVISIT_DLY_MAX      (0xFFu)
/** @brief Maximum encoded `TAS_CFG_CTRL.LIST_NUM_MAX` field value. */
#define LAN9645_TAPRIO_LIST_NUM_MAX_MAX     (0x1Fu)
/** @brief Maximum encoded `TAS_CFG_CTRL.LIST_NUM` selector field value. */
#define LAN9645_TAPRIO_LIST_NUM_MAX         (0x1Fu)
/** @brief Maximum encoded `TAS_LIST_STATE.LIST_STATE` field value. */
#define LAN9645_TAPRIO_LIST_STATE_MAX       (0x7u)
/** @brief Maximum encoded `TAS_GATE_STATE.TAS_GATE_STATE` gate mask value. */
#define LAN9645_TAPRIO_GATE_MASK_MAX        (0xFFu)

/**
 * @brief Snapshot of global TAS/TAPRIO scheduler configuration.
 */
typedef struct {
  uint8_t revisit_dly;        /**< `TAS_STATEMACHINE_CFG.REVISIT_DLY` (`0..255`). */
  uint8_t list_num_max;       /**< `TAS_CFG_CTRL.LIST_NUM_MAX` (`0..31`). */
  uint8_t list_num_selected;  /**< `TAS_CFG_CTRL.LIST_NUM` selector (`0..31`). */
  uint8_t always_guard_band;  /**< `TAS_CFG_CTRL.ALWAYS_GUARD_BAND_SCH_Q` (`0|1`). */
} lan9645_taprio_cfg_t;

/**
 * @brief Applies deterministic TAPRIO baseline with TAS lists in ADMIN state.
 *
 * Baseline policy:
 * - configure TAS scheduler global defaults (`LIST_NUM_MAX`, guard-band policy)
 * - force active TAS lists used by managed ports into ADMIN state
 * - restore per-port TAS gate state to all-queues-open (`0xFF`) on ports 0/1/8
 *
 * @param[in,out] core Initialized LAN9645 core context.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_taprio_apply_baseline(lan9645_core_t *core);

/**
 * @brief Reads global TAS/TAPRIO scheduler configuration.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[out] out Decoded global TAS configuration snapshot.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_taprio_get_cfg(lan9645_core_t *core, lan9645_taprio_cfg_t *out);

/**
 * @brief Writes global TAS/TAPRIO scheduler configuration with readback verification.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] revisit_dly `REVISIT_DLY` value (`0..255`).
 * @param[in] list_num_max `LIST_NUM_MAX` value (`0..31`).
 * @param[in] always_guard_band `ALWAYS_GUARD_BAND_SCH_Q` value (`0|1`).
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_taprio_set_cfg(
  lan9645_core_t *core,
  uint8_t revisit_dly,
  uint8_t list_num_max,
  uint8_t always_guard_band
);

/**
 * @brief Reads one TAS list state after selecting list index.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] list_num TAS list index selector (`0..31`).
 * @param[out] out_list_state Decoded list state (`0..7`).
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_taprio_get_list_state(
  lan9645_core_t *core,
  uint8_t list_num,
  uint8_t *out_list_state
);

/**
 * @brief Writes one TAS list state after selecting list index and verifies readback.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] list_num TAS list index selector (`0..31`).
 * @param[in] list_state Encoded list state (`0..7`).
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_taprio_set_list_state(
  lan9645_core_t *core,
  uint8_t list_num,
  uint8_t list_state
);

/**
 * @brief Reads per-port TAS gate-state mask.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] port Port scheduler position selector.
 * @param[out] out_gate_mask Decoded gate-state mask (`0..0xFF`).
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_taprio_get_port_gate_state(
  lan9645_core_t *core,
  uint8_t port,
  uint8_t *out_gate_mask
);

/**
 * @brief Writes per-port TAS gate-state mask and verifies readback.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] port Port scheduler position selector.
 * @param[in] gate_mask Gate-state mask (`0..0xFF`).
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_taprio_set_port_gate_state(
  lan9645_core_t *core,
  uint8_t port,
  uint8_t gate_mask
);

#ifdef __cplusplus
}
#endif

#endif /* LAN9645_TAPRIO_H */
