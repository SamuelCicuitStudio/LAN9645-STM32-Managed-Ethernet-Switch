#ifndef LAN9645_VCAP_H
#define LAN9645_VCAP_H

/**
 * @file lan9645_vcap.h
 * @brief Baseline VCAP-domain helpers for LAN9645 managed mode.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "app_common_types.h"
#include "lan9645/lan9645_core.h"

/** @brief Maximum managed VCAP selector value for `S1_SMAC_ENA`. */
#define LAN9645_VCAP_S1_SMAC_ENA_MAX            (7u)
/** @brief Maximum managed VCAP selector value for `S1_DMAC_DIP_ENA`. */
#define LAN9645_VCAP_S1_DMAC_DIP_ENA_MAX        (7u)
/** @brief Maximum managed VCAP selector value for `S1_VLAN_INNER_TAG_ENA`. */
#define LAN9645_VCAP_S1_VLAN_INNER_TAG_ENA_MAX  (7u)
/** @brief Maximum managed VCAP selector value for `PAG_VAL`. */
#define LAN9645_VCAP_S1_PAG_VAL_MAX             (0xFFu)
/** @brief Maximum managed VCAP selector value for `S2_ISDX_ENA`. */
#define LAN9645_VCAP_S2_ISDX_ENA_MAX            (3u)
/** @brief Maximum managed VCAP selector value for `S2_UDP_PAYLOAD_ENA`. */
#define LAN9645_VCAP_S2_UDP_PAYLOAD_ENA_MAX     (3u)
/** @brief Maximum managed VCAP selector value for `S2_ETYPE_PAYLOAD_ENA`. */
#define LAN9645_VCAP_S2_ETYPE_PAYLOAD_ENA_MAX   (3u)
/** @brief Maximum managed VCAP selector value for `S2_SNAP_DIS`. */
#define LAN9645_VCAP_S2_SNAP_DIS_MAX            (3u)
/** @brief Maximum managed VCAP selector value for `S2_ARP_DIS`. */
#define LAN9645_VCAP_S2_ARP_DIS_MAX             (3u)
/** @brief Maximum managed VCAP selector value for `S2_IP_TCPUDP_DIS`. */
#define LAN9645_VCAP_S2_IP_TCPUDP_DIS_MAX       (3u)
/** @brief Maximum managed VCAP selector value for `S2_IP_OTHER_DIS`. */
#define LAN9645_VCAP_S2_IP_OTHER_DIS_MAX        (3u)
/** @brief Maximum managed VCAP selector value for `S2_IP6_CFG`. */
#define LAN9645_VCAP_S2_IP6_CFG_MAX             (15u)
/** @brief Maximum managed VCAP selector value for `S2_OAM_DIS`. */
#define LAN9645_VCAP_S2_OAM_DIS_MAX             (3u)

/**
 * @brief One managed-port VCAP S1/S2 configuration snapshot.
 */
typedef struct {
  uint8_t port;                   /**< Managed switch port index. */
  uint8_t s1_smac_ena;            /**< `ANA_VCAP_CFG.S1_SMAC_ENA` */
  uint8_t s1_ena;                 /**< `ANA_VCAP_CFG.S1_ENA` */
  uint8_t s1_dmac_dip_ena;        /**< `ANA_VCAP_CFG.S1_DMAC_DIP_ENA` */
  uint8_t s1_vlan_inner_tag_ena;  /**< `ANA_VCAP_CFG.S1_VLAN_INNER_TAG_ENA` */
  uint8_t s1_pag_val;             /**< `ANA_VCAP_CFG.PAG_VAL` */
  uint8_t s2_isdx_ena;            /**< `ANA_VCAP_S2_CFG.ISDX_ENA` */
  uint8_t s2_udp_payload_ena;     /**< `ANA_VCAP_S2_CFG.UDP_PAYLOAD_ENA` */
  uint8_t s2_etype_payload_ena;   /**< `ANA_VCAP_S2_CFG.ETYPE_PAYLOAD_ENA` */
  uint8_t s2_ena;                 /**< `ANA_VCAP_S2_CFG.ENA` */
  uint8_t s2_snap_dis;            /**< `ANA_VCAP_S2_CFG.SNAP_DIS` */
  uint8_t s2_arp_dis;             /**< `ANA_VCAP_S2_CFG.ARP_DIS` */
  uint8_t s2_ip_tcpudp_dis;       /**< `ANA_VCAP_S2_CFG.IP_TCPUDP_DIS` */
  uint8_t s2_ip_other_dis;        /**< `ANA_VCAP_S2_CFG.IP_OTHER_DIS` */
  uint8_t s2_ip6_cfg;             /**< `ANA_VCAP_S2_CFG.IP6_CFG` */
  uint8_t s2_oam_dis;             /**< `ANA_VCAP_S2_CFG.OAM_DIS` */
  uint32_t s1_raw;                /**< Raw `ANA_VCAP_CFG` register value. */
  uint32_t s2_raw;                /**< Raw `ANA_VCAP_S2_CFG` register value. */
} lan9645_vcap_port_cfg_t;

/**
 * @brief Initializes VCAP baseline service state.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_vcap_init(void);

/**
 * @brief Reads one managed-port VCAP S1/S2 configuration snapshot.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] port Managed switch port index.
 * @param[out] out Destination VCAP snapshot.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_vcap_get_port_cfg(
  lan9645_core_t *core,
  uint8_t port,
  lan9645_vcap_port_cfg_t *out
);

/**
 * @brief Writes one managed-port VCAP S1/S2 configuration with readback verification.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] cfg Source VCAP snapshot.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_vcap_set_port_cfg(
  lan9645_core_t *core,
  const lan9645_vcap_port_cfg_t *cfg
);

/**
 * @brief Applies deterministic VCAP baseline.
 *
 * Baseline policy:
 * - disable S1 and S2 VCAP classification on managed ports `0/1/8`
 * - clear S1/S2 payload and key-mode selects to deterministic zero values
 *
 * This keeps ACL pipeline behavior explicit and stable until rule-programming
 * services are added.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_vcap_apply_baseline(lan9645_core_t *core);

#ifdef __cplusplus
}
#endif

#endif /* LAN9645_VCAP_H */
