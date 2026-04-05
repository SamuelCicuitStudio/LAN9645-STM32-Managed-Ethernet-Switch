#ifndef LAN9645_STATS_H
#define LAN9645_STATS_H

/**
 * @file lan9645_stats.h
 * @brief Baseline SYS counter service for LAN9645 managed firmware diagnostics.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "app_common_types.h"
#include "lan9645/lan9645_core.h"

/** @brief Maximum SYS_STAT_CFG view index supported by hardware field width. */
#define LAN9645_STATS_VIEW_MAX         (1023u)
/** @brief Port-counter clear selector used for baseline `cnt clear <view>`. */
#define LAN9645_STATS_CLEAR_SEL_PORTS  (0x07u)

/**
 * @brief Snapshot of baseline SYS counters for one selected STAT_VIEW.
 */
typedef struct {
  uint16_t view;       /**< Active SYS stat view index used for the sample. */
  uint32_t rx_oct;     /**< `SYS_CNT[0x000]` RX octets counter for selected view. */
  uint32_t tx_oct;     /**< `SYS_CNT[0x080]` TX octets counter for selected view. */
  uint32_t dr_local;   /**< `SYS_CNT[0x100]` drop-local counter for selected view. */
  uint8_t sw_paused;   /**< `SYS_SW_STATUS(view).PORT_RX_PAUSED` when available. */
} lan9645_stats_sample_t;

/**
 * @brief Initializes the LAN9645 stats service state.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_stats_init(void);

/**
 * @brief Reads baseline SYS counters for one `STAT_VIEW` index.
 *
 * @param[in,out] core LAN9645 core context.
 * @param[in] view SYS stat view index.
 * @param[out] out Destination sample snapshot.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_stats_read(lan9645_core_t *core, uint16_t view, lan9645_stats_sample_t *out);

/**
 * @brief Triggers clear-shot for baseline port counters at one `STAT_VIEW`.
 *
 * @param[in,out] core LAN9645 core context.
 * @param[in] view SYS stat view index.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_stats_clear(lan9645_core_t *core, uint16_t view);

#ifdef __cplusplus
}
#endif

#endif /* LAN9645_STATS_H */
