#ifndef LAN9645_PSFP_H
#define LAN9645_PSFP_H

/**
 * @file lan9645_psfp.h
 * @brief Baseline PSFP (IEEE 802.1Qci) control helpers for LAN9645 managed mode.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "app_common_types.h"
#include "lan9645/lan9645_core.h"

/**
 * @brief Initializes PSFP baseline service state.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_psfp_init(void);

/** @brief Maximum SFID/SGID index value (`7-bit`). */
#define LAN9645_PSFP_INDEX_MAX         (0x7Fu)
/** @brief Maximum SFID `MAX_SDU_LEN` value (`16-bit`). */
#define LAN9645_PSFP_MAX_SDU_LEN_MAX   (0xFFFFu)
/** @brief Maximum SG `LIST_LENGTH` value (`3-bit`). */
#define LAN9645_PSFP_LIST_LENGTH_MAX   (0x7u)
/** @brief Maximum SG `INIT_IPS` value (`4-bit`). */
#define LAN9645_PSFP_INIT_IPS_MAX      (0xFu)

/**
 * @brief Snapshot of one SFID entry used by PSFP.
 */
typedef struct {
  uint8_t sfid_index;     /**< SFID table index (`0..127`). */
  uint8_t bo_frm;         /**< `B_O_FRM` (`0|1`). */
  uint8_t bo_frm_enable;  /**< `B_O_FRM_ENA` (`0|1`). */
  uint8_t force_block;    /**< `FORCE_BLOCK` (`0|1`). */
  uint16_t max_sdu_len;   /**< `MAX_SDU_LEN` (`0..65535`). */
} lan9645_psfp_sfid_cfg_t;

/**
 * @brief Snapshot of one stream-gate base configuration (`ANA_SG_CFG_3`).
 */
typedef struct {
  uint8_t sgid;              /**< SG index (`0..127`). */
  uint8_t list_length;       /**< `LIST_LENGTH` (`0..7`). */
  uint8_t gate_enable;       /**< `GATE_ENABLE` (`0|1`). */
  uint8_t init_ips;          /**< `INIT_IPS` (`0..15`). */
  uint8_t init_gate_state;   /**< `INIT_GATE_STATE` (`0|1`). */
} lan9645_psfp_gate_cfg_t;

/**
 * @brief Applies deterministic PSFP baseline with stream-gate/filter disabled.
 *
 * Baseline policy:
 * - reset SFID index 0 table entry using `ANA_SFIDTIDX`/`ANA_SFIDACCESS`
 * - reset SGID index 0 gate config and 4 GCL entries
 * - trigger and wait for SG configuration commit completion
 *
 * @param[in,out] core Initialized LAN9645 core context.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_psfp_apply_baseline(lan9645_core_t *core);

/**
 * @brief Reads one PSFP SFID entry snapshot.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] sfid_index SFID index (`0..127`).
 * @param[out] out Decoded SFID snapshot.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_psfp_get_sfid_cfg(
  lan9645_core_t *core,
  uint8_t sfid_index,
  lan9645_psfp_sfid_cfg_t *out
);

/**
 * @brief Writes one PSFP SFID entry and waits for SFID table command completion.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] sfid_index SFID index (`0..127`).
 * @param[in] bo_frm `B_O_FRM` (`0|1`).
 * @param[in] bo_frm_enable `B_O_FRM_ENA` (`0|1`).
 * @param[in] force_block `FORCE_BLOCK` (`0|1`).
 * @param[in] max_sdu_len `MAX_SDU_LEN` (`0..65535`).
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_psfp_set_sfid_cfg(
  lan9645_core_t *core,
  uint8_t sfid_index,
  uint8_t bo_frm,
  uint8_t bo_frm_enable,
  uint8_t force_block,
  uint16_t max_sdu_len
);

/**
 * @brief Reads one PSFP stream-gate base configuration (`ANA_SG_CFG_3`).
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] sgid SG index (`0..127`).
 * @param[out] out Decoded gate configuration snapshot.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_psfp_get_gate_cfg(
  lan9645_core_t *core,
  uint8_t sgid,
  lan9645_psfp_gate_cfg_t *out
);

/**
 * @brief Writes one PSFP stream-gate base configuration (`ANA_SG_CFG_3`).
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] sgid SG index (`0..127`).
 * @param[in] list_length `LIST_LENGTH` (`0..7`).
 * @param[in] gate_enable `GATE_ENABLE` (`0|1`).
 * @param[in] init_ips `INIT_IPS` (`0..15`).
 * @param[in] init_gate_state `INIT_GATE_STATE` (`0|1`).
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_psfp_set_gate_cfg(
  lan9645_core_t *core,
  uint8_t sgid,
  uint8_t list_length,
  uint8_t gate_enable,
  uint8_t init_ips,
  uint8_t init_gate_state
);

/**
 * @brief Commits one PSFP stream-gate configuration and waits for completion.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] sgid SG index (`0..127`) to commit.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_psfp_commit_gate_cfg(lan9645_core_t *core, uint8_t sgid);

#ifdef __cplusplus
}
#endif

#endif /* LAN9645_PSFP_H */
