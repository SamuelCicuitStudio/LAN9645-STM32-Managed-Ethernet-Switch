#ifndef LAN9645_STREAM_H
#define LAN9645_STREAM_H

/**
 * @file lan9645_stream.h
 * @brief Baseline stream-domain control helpers for LAN9645 managed mode.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "app_common_types.h"
#include "lan9645/lan9645_core.h"

/**
 * @brief Initializes stream baseline service state.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_stream_init(void);

/** @brief Maximum stream-filter index (`ANA_SFIDTIDX.SFID_INDEX`, 7-bit). */
#define LAN9645_STREAM_SFID_INDEX_MAX    (0x7Fu)
/** @brief Maximum `ANA_SFIDACCESS.MAX_SDU_LEN` value (16-bit). */
#define LAN9645_STREAM_MAX_SDU_LEN_MAX   (0xFFFFu)

/**
 * @brief Snapshot of one stream filter SFID entry configuration.
 */
typedef struct {
  uint8_t sfid_index;     /**< SFID table index (`0..127`). */
  uint8_t bo_frm;         /**< `B_O_FRM` bit (`0|1`). */
  uint8_t bo_frm_enable;  /**< `B_O_FRM_ENA` bit (`0|1`). */
  uint8_t force_block;    /**< `FORCE_BLOCK` bit (`0|1`). */
  uint16_t max_sdu_len;   /**< `MAX_SDU_LEN` field (`0..65535`). */
} lan9645_stream_sfid_cfg_t;

/**
 * @brief Applies deterministic stream baseline configuration.
 *
 * Baseline policy:
 * - reset stream filter index 0 to disabled/default state
 * - commit through `ANA_SFIDACCESS` write command
 * - wait until SFID table command returns idle
 *
 * @param[in,out] core Initialized LAN9645 core context.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_stream_apply_baseline(lan9645_core_t *core);

/**
 * @brief Reads one SFID entry snapshot.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] sfid_index SFID index (`0..127`).
 * @param[out] out Decoded SFID entry snapshot.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_stream_get_sfid_cfg(
  lan9645_core_t *core,
  uint8_t sfid_index,
  lan9645_stream_sfid_cfg_t *out
);

/**
 * @brief Writes one SFID entry and waits for SFID command completion.
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
app_status_t lan9645_stream_set_sfid_cfg(
  lan9645_core_t *core,
  uint8_t sfid_index,
  uint8_t bo_frm,
  uint8_t bo_frm_enable,
  uint8_t force_block,
  uint16_t max_sdu_len
);

#ifdef __cplusplus
}
#endif

#endif /* LAN9645_STREAM_H */
