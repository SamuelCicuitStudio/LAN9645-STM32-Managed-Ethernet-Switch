#ifndef LAN9645_TC_FLOWER_H
#define LAN9645_TC_FLOWER_H

/**
 * @file lan9645_tc_flower.h
 * @brief Baseline tc-flower domain helpers for LAN9645 managed mode.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "app_common_types.h"
#include "lan9645/lan9645_core.h"

/**
 * @brief One tc-flower runtime state snapshot for one managed port.
 */
typedef struct {
  uint8_t port;                   /**< Managed switch port index. */
  uint8_t enable;                 /**< High-level tc-flower enable state (`S1_ENA != 0`). */
  uint8_t s1_smac_ena;            /**< `ANA_VCAP_CFG.S1_SMAC_ENA` */
  uint8_t s1_ena;                 /**< `ANA_VCAP_CFG.S1_ENA` */
  uint8_t s1_dmac_dip_ena;        /**< `ANA_VCAP_CFG.S1_DMAC_DIP_ENA` */
  uint8_t s1_vlan_inner_tag_ena;  /**< `ANA_VCAP_CFG.S1_VLAN_INNER_TAG_ENA` */
  uint8_t s1_pag_val;             /**< `ANA_VCAP_CFG.PAG_VAL` */
} lan9645_tc_flower_state_t;

/**
 * @brief Initializes tc-flower baseline service state.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_tc_flower_init(void);

/**
 * @brief Reads one managed-port tc-flower runtime state.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] port Managed switch port index.
 * @param[out] out Destination tc-flower state snapshot.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_tc_flower_get_state(
  lan9645_core_t *core,
  uint8_t port,
  lan9645_tc_flower_state_t *out
);

/**
 * @brief Enables or disables tc-flower classification state on one managed port.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] port Managed switch port index.
 * @param[in] enable Set to `1` to enable tc-flower S1 classification path, `0` to disable.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_tc_flower_set_enable(
  lan9645_core_t *core,
  uint8_t port,
  uint8_t enable
);

/**
 * @brief Applies deterministic tc-flower baseline.
 *
 * Baseline policy:
 * - force ACL pipeline to the same deterministic no-rule state as VCAP baseline
 * - keep tc-flower domain explicitly register-backed for later rule insertion
 *
 * @param[in,out] core Initialized LAN9645 core context.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_tc_flower_apply_baseline(lan9645_core_t *core);

#ifdef __cplusplus
}
#endif

#endif /* LAN9645_TC_FLOWER_H */
