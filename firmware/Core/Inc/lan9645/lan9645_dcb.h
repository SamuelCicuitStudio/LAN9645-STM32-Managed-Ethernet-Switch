#ifndef LAN9645_DCB_H
#define LAN9645_DCB_H

/**
 * @file lan9645_dcb.h
 * @brief DCB helpers for LAN9645 managed mode.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "app_common_types.h"
#include "lan9645/lan9645_core.h"

/** @brief Maximum ingress/egress PFC bit-mask width (8 traffic classes). */
#define LAN9645_DCB_PFC_MASK_MAX      (0xFFu)
/** @brief Maximum ingress default priority value. */
#define LAN9645_DCB_DEFAULT_PRIO_MAX  (7u)
/** @brief Maximum ingress default drop-precedence value. */
#define LAN9645_DCB_DEFAULT_DP_MAX    (1u)

/**
 * @brief One DCB per-port configuration snapshot.
 */
typedef struct {
  uint8_t port;              /**< Managed port index. */
  uint8_t trust_pcp;         /**< `ANA_QOS_CFG.QOS_PCP_ENA` */
  uint8_t trust_dscp;        /**< `ANA_QOS_CFG.QOS_DSCP_ENA` */
  uint8_t default_prio;      /**< `ANA_QOS_CFG.QOS_DEFAULT_VAL` */
  uint8_t default_dp;        /**< `ANA_QOS_CFG.DP_DEFAULT_VAL` */
  uint8_t vlan_pfc_enable;   /**< `ANA_VLAN_CFG.VLAN_PFC_ENA` */
  uint8_t rx_pfc_mask;       /**< `ANA_PFC_CFG.RX_PFC_ENA` mask. */
  uint8_t tx_pfc_mask;       /**< `QSYS_SW_PORT_MODE.TX_PFC_ENA` mask. */
} lan9645_dcb_port_cfg_t;

/**
 * @brief Initializes DCB baseline service state.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_dcb_init(void);

/**
 * @brief Reads one DCB per-port configuration snapshot.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] port Managed port index.
 * @param[out] out Destination DCB snapshot.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_dcb_get_port_cfg(
  lan9645_core_t *core,
  uint8_t port,
  lan9645_dcb_port_cfg_t *out
);

/**
 * @brief Writes one DCB per-port configuration with readback verification.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] port Managed port index.
 * @param[in] trust_pcp New PCP trust enable bit.
 * @param[in] trust_dscp New DSCP trust enable bit.
 * @param[in] default_prio New default ingress priority.
 * @param[in] default_dp New default ingress drop-precedence.
 * @param[in] vlan_pfc_enable New VLAN PFC enable bit.
 * @param[in] rx_pfc_mask New ingress PFC queue mask.
 * @param[in] tx_pfc_mask New egress PFC queue mask.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_dcb_set_port_cfg(
  lan9645_core_t *core,
  uint8_t port,
  uint8_t trust_pcp,
  uint8_t trust_dscp,
  uint8_t default_prio,
  uint8_t default_dp,
  uint8_t vlan_pfc_enable,
  uint8_t rx_pfc_mask,
  uint8_t tx_pfc_mask
);

/**
 * @brief Applies deterministic DCB baseline (PFC disabled + trust baseline).
 *
 * Baseline policy:
 * - on managed ports `0/1/8`, set ingress defaults to priority/DPL `0`
 * - keep PCP trust enabled and DSCP trust disabled
 * - disable RX/TX PFC bits across `ANA_VLAN_CFG`, `ANA_PFC_CFG`, `QSYS_SW_PORT_MODE`
 *
 * @param[in,out] core Initialized LAN9645 core context.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_dcb_apply_baseline(lan9645_core_t *core);

#ifdef __cplusplus
}
#endif

#endif /* LAN9645_DCB_H */
