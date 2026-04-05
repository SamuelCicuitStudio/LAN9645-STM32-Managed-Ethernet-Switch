#ifndef LAN9645_QOS_H
#define LAN9645_QOS_H

/**
 * @file lan9645_qos.h
 * @brief Baseline QoS, mqprio, and ETS helpers for LAN9645 managed mode.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "app_common_types.h"
#include "lan9645/lan9645_core.h"

#define LAN9645_QOS_SCHED_QUEUE_COUNT (8u)
#define LAN9645_QOS_DWRR_COST_MAX     (31u)
#define LAN9645_QOS_DEFAULT_PRIO_MAX  (7u)
#define LAN9645_QOS_DEFAULT_DP_MAX    (1u)
#define LAN9645_QOS_DSCP_MAX          (63u)
#define LAN9645_QOS_PCP_MAX           (7u)
#define LAN9645_QOS_DEI_MAX           (1u)
#define LAN9645_QOS_REW_TAG_CFG_MAX   (3u)
#define LAN9645_QOS_REW_TAG_MODE_MAX  (3u)

/**
 * @brief Scheduler mode policy used by `mqprio` and `ets` control paths.
 */
typedef enum {
  LAN9645_QOS_SCHED_STRICT = 0,
  LAN9645_QOS_SCHED_DWRR   = 1
} lan9645_qos_sched_mode_t;

/**
 * @brief Snapshot of one managed-port scheduler state.
 */
typedef struct {
  uint8_t port;
  lan9645_qos_sched_mode_t mode;
  uint8_t rr_enable;
  uint8_t dwrr_count;
  uint8_t cost[LAN9645_QOS_SCHED_QUEUE_COUNT];
} lan9645_qos_sched_state_t;

/**
 * @brief Snapshot of per-port QoS trust and default-priority policy.
 */
typedef struct {
  uint8_t port;
  uint8_t trust_pcp;
  uint8_t trust_dscp;
  uint8_t default_prio;
  uint8_t default_dp;
} lan9645_qos_port_cfg_t;

/**
 * @brief Snapshot of one global DSCP-map entry.
 */
typedef struct {
  uint8_t dscp;
  uint8_t qos_prio;
  uint8_t dp;
  uint8_t trust_enable;
  uint8_t rewrite_enable;
} lan9645_qos_dscp_cfg_t;

/**
 * @brief Snapshot of one per-port PCP/DEI map entry (ingress + rewrite view).
 */
typedef struct {
  uint8_t port;
  uint8_t pcp;
  uint8_t dei;
  uint8_t qos_prio;
  uint8_t dp;
  uint8_t rew_pcp;
  uint8_t rew_dei;
} lan9645_qos_pcpdei_cfg_t;

/**
 * @brief Snapshot of per-port rewrite-tag policy fields.
 */
typedef struct {
  uint8_t port;
  uint8_t tag_cfg;
  uint8_t tag_pcp_cfg;
  uint8_t tag_dei_cfg;
} lan9645_qos_rew_tag_cfg_t;

/**
 * @brief Snapshot of per-port rewrite default VLAN PCP/DEI fields.
 */
typedef struct {
  uint8_t port;
  uint8_t port_pcp;
  uint8_t port_dei;
} lan9645_qos_rew_port_cfg_t;

/**
 * @brief Initializes QoS baseline service state.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_qos_init(void);

/**
 * @brief Applies deterministic ingress/egress QoS baseline for managed ports.
 *
 * This function programs the Linux-aligned baseline for:
 * - `ANA_QOS_CFG`
 * - `ANA_PCP_DEI_CFG`
 * - `ANA_DSCP_CFG`
 * - `REW_PORT_VLAN_CFG`
 * - `REW_TAG_CFG`
 * - `REW_PCP_DEI_CFG`
 *
 * @param[in,out] core Initialized LAN9645 core context.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_qos_apply_baseline(lan9645_core_t *core);

/**
 * @brief Applies deterministic scheduler baseline for `mqprio` domain.
 *
 * Baseline behavior maps to strict scheduling with DWRR disabled for managed
 * ports by programming `QSYS_SE_CFG` and `QSYS_SE_DWRR_CFG`.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_qos_apply_mqprio_baseline(lan9645_core_t *core);

/**
 * @brief Applies deterministic scheduler baseline for `ets` domain.
 *
 * For this firmware baseline the ETS default policy is the same strict mode as
 * @ref lan9645_qos_apply_mqprio_baseline.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_qos_apply_ets_baseline(lan9645_core_t *core);

/**
 * @brief Sets scheduler mode for one managed port (`0`, `1`, or `8`).
 *
 * `LAN9645_QOS_SCHED_STRICT` forces strict-priority scheduling
 * (`SE_RR_ENA=0`, `SE_DWRR_CNT=0`, all `DWRR_COST=0`).
 *
 * `LAN9645_QOS_SCHED_DWRR` enables weighted round-robin baseline
 * (`SE_RR_ENA=1`, `SE_DWRR_CNT=7`, all `DWRR_COST=1`).
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] port Managed switch port (`0`, `1`, or `8`).
 * @param[in] mode Target scheduler mode.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_qos_set_scheduler_mode(
  lan9645_core_t *core,
  uint8_t port,
  lan9645_qos_sched_mode_t mode
);

/**
 * @brief Reads scheduler state for one managed port (`0`, `1`, or `8`).
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] port Managed switch port (`0`, `1`, or `8`).
 * @param[out] out Scheduler state snapshot.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_qos_get_scheduler_state(
  lan9645_core_t *core,
  uint8_t port,
  lan9645_qos_sched_state_t *out
);

/**
 * @brief Sets one DWRR queue cost for a managed port (`0`, `1`, or `8`).
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] port Managed switch port (`0`, `1`, or `8`).
 * @param[in] queue Queue index (`0..7`).
 * @param[in] cost DWRR queue cost (`0..31`).
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_qos_set_scheduler_weight(
  lan9645_core_t *core,
  uint8_t port,
  uint8_t queue,
  uint8_t cost
);

/**
 * @brief Reads one DWRR queue cost for a managed port (`0`, `1`, or `8`).
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] port Managed switch port (`0`, `1`, or `8`).
 * @param[in] queue Queue index (`0..7`).
 * @param[out] out_cost Readback queue cost.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_qos_get_scheduler_weight(
  lan9645_core_t *core,
  uint8_t port,
  uint8_t queue,
  uint8_t *out_cost
);

/**
 * @brief Sets per-port QoS trust policy (`PCP`/`DSCP`) with readback verification.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] port Managed switch port (`0`, `1`, or `8`).
 * @param[in] trust_pcp Enable (`1`) or disable (`0`) PCP trust.
 * @param[in] trust_dscp Enable (`1`) or disable (`0`) DSCP trust.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_qos_set_port_trust(
  lan9645_core_t *core,
  uint8_t port,
  uint8_t trust_pcp,
  uint8_t trust_dscp
);

/**
 * @brief Sets per-port default QoS values with readback verification.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] port Managed switch port (`0`, `1`, or `8`).
 * @param[in] default_prio Default QoS priority (`0..7`).
 * @param[in] default_dp Default drop precedence (`0..1`).
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_qos_set_port_default(
  lan9645_core_t *core,
  uint8_t port,
  uint8_t default_prio,
  uint8_t default_dp
);

/**
 * @brief Reads per-port QoS trust/default policy snapshot.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] port Managed switch port (`0`, `1`, or `8`).
 * @param[out] out Per-port QoS policy snapshot.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_qos_get_port_cfg(
  lan9645_core_t *core,
  uint8_t port,
  lan9645_qos_port_cfg_t *out
);

/**
 * @brief Sets one global DSCP-map entry with readback verification.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] dscp DSCP index (`0..63`).
 * @param[in] qos_prio QoS priority value (`0..7`).
 * @param[in] dp Drop precedence value (`0..1`).
 * @param[in] trust_enable DSCP trust-enable (`0|1`).
 * @param[in] rewrite_enable DSCP rewrite-enable (`0|1`).
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_qos_set_dscp_entry(
  lan9645_core_t *core,
  uint8_t dscp,
  uint8_t qos_prio,
  uint8_t dp,
  uint8_t trust_enable,
  uint8_t rewrite_enable
);

/**
 * @brief Reads one global DSCP-map entry.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] dscp DSCP index (`0..63`).
 * @param[out] out DSCP entry snapshot.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_qos_get_dscp_entry(
  lan9645_core_t *core,
  uint8_t dscp,
  lan9645_qos_dscp_cfg_t *out
);

/**
 * @brief Sets one per-port PCP/DEI map entry with readback verification.
 *
 * This updates both ingress mapping (`ANA_PCP_DEI_CFG`) and rewrite mapping
 * (`REW_PCP_DEI_CFG`) for the same `pcp/dei` selector.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] port Managed switch port (`0`, `1`, or `8`).
 * @param[in] pcp PCP selector (`0..7`).
 * @param[in] dei DEI selector (`0..1`).
 * @param[in] qos_prio Ingress QoS priority result (`0..7`).
 * @param[in] dp Ingress drop precedence result (`0..1`).
 * @param[in] rew_pcp Rewrite PCP value (`0..7`).
 * @param[in] rew_dei Rewrite DEI value (`0..1`).
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_qos_set_pcpdei_entry(
  lan9645_core_t *core,
  uint8_t port,
  uint8_t pcp,
  uint8_t dei,
  uint8_t qos_prio,
  uint8_t dp,
  uint8_t rew_pcp,
  uint8_t rew_dei
);

/**
 * @brief Reads one per-port PCP/DEI map entry (ingress + rewrite view).
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] port Managed switch port (`0`, `1`, or `8`).
 * @param[in] pcp PCP selector (`0..7`).
 * @param[in] dei DEI selector (`0..1`).
 * @param[out] out PCP/DEI entry snapshot.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_qos_get_pcpdei_entry(
  lan9645_core_t *core,
  uint8_t port,
  uint8_t pcp,
  uint8_t dei,
  lan9645_qos_pcpdei_cfg_t *out
);

/**
 * @brief Sets per-port rewrite-tag policy (`REW_TAG_CFG`) with readback verification.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] port Managed switch port (`0`, `1`, or `8`).
 * @param[in] tag_pcp_cfg Tag PCP policy selector (`0..3`).
 * @param[in] tag_dei_cfg Tag DEI policy selector (`0..3`).
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_qos_set_rew_tag_cfg(
  lan9645_core_t *core,
  uint8_t port,
  uint8_t tag_pcp_cfg,
  uint8_t tag_dei_cfg
);

/**
 * @brief Reads per-port rewrite-tag policy (`REW_TAG_CFG`) snapshot.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] port Managed switch port (`0`, `1`, or `8`).
 * @param[out] out Rewrite-tag policy snapshot.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_qos_get_rew_tag_cfg(
  lan9645_core_t *core,
  uint8_t port,
  lan9645_qos_rew_tag_cfg_t *out
);

/**
 * @brief Sets per-port rewrite tag insertion mode (`REW_TAG_CFG.TAG_CFG`) with readback verification.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] port Managed switch port (`0`, `1`, or `8`).
 * @param[in] tag_cfg Tag insertion mode selector (`0..3`).
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_qos_set_rew_tag_mode(
  lan9645_core_t *core,
  uint8_t port,
  uint8_t tag_cfg
);

/**
 * @brief Sets per-port rewrite default VLAN PCP/DEI (`REW_PORT_VLAN_CFG`) with readback verification.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] port Managed switch port (`0`, `1`, or `8`).
 * @param[in] port_pcp Port default PCP (`0..7`).
 * @param[in] port_dei Port default DEI (`0..1`).
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_qos_set_rew_port_cfg(
  lan9645_core_t *core,
  uint8_t port,
  uint8_t port_pcp,
  uint8_t port_dei
);

/**
 * @brief Reads per-port rewrite default VLAN PCP/DEI (`REW_PORT_VLAN_CFG`) snapshot.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] port Managed switch port (`0`, `1`, or `8`).
 * @param[out] out Rewrite-port policy snapshot.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_qos_get_rew_port_cfg(
  lan9645_core_t *core,
  uint8_t port,
  lan9645_qos_rew_port_cfg_t *out
);

#ifdef __cplusplus
}
#endif

#endif /* LAN9645_QOS_H */
