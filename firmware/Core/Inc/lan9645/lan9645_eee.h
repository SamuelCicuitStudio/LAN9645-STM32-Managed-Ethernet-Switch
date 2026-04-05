#ifndef LAN9645_EEE_H
#define LAN9645_EEE_H

/**
 * @file lan9645_eee.h
 * @brief EEE (Energy Efficient Ethernet) helpers for LAN9645 managed mode.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "app_common_types.h"
#include "lan9645/lan9645_core.h"

/** @brief Maximum encoded EEE wakeup timer value (7-bit field). */
#define LAN9645_EEE_WAKEUP_TIMER_MAX  (127u)
/** @brief Maximum encoded EEE holdoff timer value (7-bit field). */
#define LAN9645_EEE_HOLDOFF_TIMER_MAX (127u)

/**
 * @brief One DEV_EEE_CFG port snapshot.
 */
typedef struct {
  uint8_t port;           /**< DEV target port index. */
  uint8_t eee_enable;     /**< `DEV_EEE_CFG.EEE_ENA` state. */
  uint8_t wakeup_timer;   /**< `DEV_EEE_CFG.EEE_TIMER_WAKEUP` field. */
  uint8_t holdoff_timer;  /**< `DEV_EEE_CFG.EEE_TIMER_HOLDOFF` field. */
  uint8_t age_timer;      /**< `DEV_EEE_CFG.EEE_TIMER_AGE` field. */
  uint8_t port_lpi;       /**< `DEV_EEE_CFG.PORT_LPI` read-only runtime state. */
} lan9645_eee_port_cfg_t;

/**
 * @brief Initializes EEE baseline service state.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_eee_init(void);

/**
 * @brief Reads one EEE port configuration snapshot.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] port DEV target port index.
 * @param[out] out Destination EEE snapshot.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_eee_get_port_cfg(
  lan9645_core_t *core,
  uint8_t port,
  lan9645_eee_port_cfg_t *out
);

/**
 * @brief Updates one EEE port configuration.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] port DEV target port index.
 * @param[in] eee_enable New `EEE_ENA` bit value.
 * @param[in] wakeup_timer New `EEE_TIMER_WAKEUP` field value.
 * @param[in] holdoff_timer New `EEE_TIMER_HOLDOFF` field value.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_eee_set_port_cfg(
  lan9645_core_t *core,
  uint8_t port,
  uint8_t eee_enable,
  uint8_t wakeup_timer,
  uint8_t holdoff_timer
);

/**
 * @brief Applies deterministic EEE baseline for managed ports.
 *
 * Baseline policy:
 * - clear `DEV_EEE_CFG.EEE_ENA` on managed ports `0/1/8`
 * - clear `DEV_EEE_CFG.EEE_TIMER_WAKEUP` on managed ports `0/1/8`
 * - clear `DEV_EEE_CFG.EEE_TIMER_HOLDOFF` on managed ports `0/1/8`
 *
 * @param[in,out] core Initialized LAN9645 core context.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_eee_apply_baseline(lan9645_core_t *core);

#ifdef __cplusplus
}
#endif

#endif /* LAN9645_EEE_H */
