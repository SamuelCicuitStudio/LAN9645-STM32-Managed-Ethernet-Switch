#ifndef LAN9645_TC_MATCHALL_H
#define LAN9645_TC_MATCHALL_H

/**
 * @file lan9645_tc_matchall.h
 * @brief Baseline tc-matchall domain helpers for LAN9645 managed mode.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "app_common_types.h"
#include "lan9645/lan9645_core.h"

/**
 * @brief One tc-matchall runtime state snapshot for one managed port.
 */
typedef struct {
  uint8_t port;                /**< Managed switch port index. */
  uint8_t enable;              /**< High-level tc-matchall enable state (`S2_ENA != 0`). */
  uint8_t s2_ena;              /**< `ANA_VCAP_S2_CFG.ENA` */
  uint8_t s2_isdx_ena;         /**< `ANA_VCAP_S2_CFG.ISDX_ENA` */
  uint8_t s2_udp_payload_ena;  /**< `ANA_VCAP_S2_CFG.UDP_PAYLOAD_ENA` */
  uint8_t s2_etype_payload_ena;/**< `ANA_VCAP_S2_CFG.ETYPE_PAYLOAD_ENA` */
  uint8_t s2_ip6_cfg;          /**< `ANA_VCAP_S2_CFG.IP6_CFG` */
} lan9645_tc_matchall_state_t;

/**
 * @brief Initializes tc-matchall baseline service state.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_tc_matchall_init(void);

/**
 * @brief Reads one managed-port tc-matchall runtime state.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] port Managed switch port index.
 * @param[out] out Destination tc-matchall state snapshot.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_tc_matchall_get_state(
  lan9645_core_t *core,
  uint8_t port,
  lan9645_tc_matchall_state_t *out
);

/**
 * @brief Enables or disables tc-matchall classification state on one managed port.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] port Managed switch port index.
 * @param[in] enable Set to `1` to enable tc-matchall S2 classification path, `0` to disable.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_tc_matchall_set_enable(
  lan9645_core_t *core,
  uint8_t port,
  uint8_t enable
);

/**
 * @brief Applies deterministic tc-matchall baseline.
 *
 * Baseline policy:
 * - force classifier pipeline to deterministic no-rule state
 * - keep tc-matchall domain explicitly register-backed for later action
 *   programming
 *
 * @param[in,out] core Initialized LAN9645 core context.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_tc_matchall_apply_baseline(lan9645_core_t *core);

#ifdef __cplusplus
}
#endif

#endif /* LAN9645_TC_MATCHALL_H */
