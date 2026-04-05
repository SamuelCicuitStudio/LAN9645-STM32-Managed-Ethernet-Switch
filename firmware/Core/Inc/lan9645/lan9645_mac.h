#ifndef LAN9645_MAC_H
#define LAN9645_MAC_H

/**
 * @file lan9645_mac.h
 * @brief MAC-domain helpers for LAN9645 managed mode.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "app_common_types.h"
#include "lan9645/lan9645_core.h"

/**
 * @brief One MAC per-port configuration snapshot.
 */
typedef struct {
  uint8_t port;         /**< Managed port index. */
  uint8_t admin_up;     /**< Administrative forwarding state. */
  uint16_t speed_mbps;  /**< Target speed (`10` or `100`). */
  uint8_t full_duplex;  /**< Duplex mode (`1=full`, `0=half`). */
  uint8_t mac_rx_ena;   /**< MAC RX enable state. */
  uint8_t mac_tx_ena;   /**< MAC TX enable state. */
} lan9645_mac_port_cfg_t;

/**
 * @brief Initializes MAC baseline service state.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_mac_init(void);

/**
 * @brief Reads one MAC per-port configuration snapshot.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] port Managed port index.
 * @param[out] out Destination MAC snapshot.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_mac_get_port_cfg(
  lan9645_core_t *core,
  uint8_t port,
  lan9645_mac_port_cfg_t *out
);

/**
 * @brief Writes one MAC per-port configuration with readback verification.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] port Managed port index.
 * @param[in] admin_up New administrative forwarding state.
 * @param[in] speed_mbps New target speed (`10` or `100`).
 * @param[in] full_duplex New duplex mode (`1=full`, `0=half`).
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_mac_set_port_cfg(
  lan9645_core_t *core,
  uint8_t port,
  uint8_t admin_up,
  uint16_t speed_mbps,
  uint8_t full_duplex
);

/**
 * @brief Applies deterministic MAC baseline on managed ports.
 *
 * Baseline policy:
 * - enforce port `0`, `1`, and `8` to `100M/full`
 * - enforce administrative up
 * - verify admin/speed/duplex/RX/TX status through readback
 *
 * @param[in,out] core Initialized LAN9645 core context.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_mac_apply_baseline(lan9645_core_t *core);

#ifdef __cplusplus
}
#endif

#endif /* LAN9645_MAC_H */
