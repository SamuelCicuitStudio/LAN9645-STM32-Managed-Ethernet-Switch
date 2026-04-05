#ifndef LAN9645_LAG_H
#define LAN9645_LAG_H

/**
 * @file lan9645_lag.h
 * @brief Baseline LAG hash-domain helpers for LAN9645 managed mode.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "app_common_types.h"
#include "lan9645/lan9645_core.h"

/** @brief Maximum boolean selector value in LAG runtime configuration. */
#define LAN9645_LAG_BOOL_MAX (1u)

/**
 * @brief LAG hash-key runtime configuration snapshot.
 */
typedef struct {
  uint8_t ac_rnd_ena;           /**< `ANA_AGGR_CFG.AC_RND_ENA` */
  uint8_t ac_dmac_ena;          /**< `ANA_AGGR_CFG.AC_DMAC_ENA` */
  uint8_t ac_smac_ena;          /**< `ANA_AGGR_CFG.AC_SMAC_ENA` */
  uint8_t ac_ip6_flow_lbl_ena;  /**< `ANA_AGGR_CFG.AC_IP6_FLOW_LBL_ENA` */
  uint8_t ac_ip6_tcpudp_ena;    /**< `ANA_AGGR_CFG.AC_IP6_TCPUDP_ENA` */
  uint8_t ac_ip4_sipdip_ena;    /**< `ANA_AGGR_CFG.AC_IP4_SIPDIP_ENA` */
  uint8_t ac_ip4_tcpudp_ena;    /**< `ANA_AGGR_CFG.AC_IP4_TCPUDP_ENA` */
} lan9645_lag_cfg_t;

/**
 * @brief Initializes LAG baseline service state.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_lag_init(void);

/**
 * @brief Reads LAG hash runtime configuration.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[out] out Destination configuration snapshot.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_lag_get_cfg(lan9645_core_t *core, lan9645_lag_cfg_t *out);

/**
 * @brief Writes LAG hash runtime configuration with readback verification.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] cfg Source configuration.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_lag_set_cfg(lan9645_core_t *core, const lan9645_lag_cfg_t *cfg);

/**
 * @brief Applies deterministic LAG hash baseline.
 *
 * Baseline policy is Linux-aligned `L2` hashing:
 * - enable `ANA_AGGR_CFG.AC_DMAC_ENA`
 * - enable `ANA_AGGR_CFG.AC_SMAC_ENA`
 * - clear other aggregation-key enables
 *
 * @param[in,out] core Initialized LAN9645 core context.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_lag_apply_baseline(lan9645_core_t *core);

#ifdef __cplusplus
}
#endif

#endif /* LAN9645_LAG_H */
