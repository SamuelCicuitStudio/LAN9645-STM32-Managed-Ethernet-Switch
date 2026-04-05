#ifndef LAN9645_PHYLINK_H
#define LAN9645_PHYLINK_H

/**
 * @file lan9645_phylink.h
 * @brief Phylink-domain helpers for LAN9645 managed mode.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "app_common_types.h"
#include "lan9645/lan9645_core.h"

/** @brief Maximum valid RGMII interface index. */
#define LAN9645_PHYLINK_RGMII_IDX_MAX  (1u)
/** @brief Maximum valid TX clock configuration selector. */
#define LAN9645_PHYLINK_TX_CLK_CFG_MAX (7u)

/**
 * @brief One RGMII interface configuration/status snapshot.
 */
typedef struct {
  uint8_t rgmii_idx;     /**< RGMII interface index. */
  uint8_t ib_enable;     /**< `HSIO_RGMII_CFG.IB_ENA` */
  uint8_t ib_rx_enable;  /**< `HSIO_RGMII_CFG.IB_RX_ENA` */
  uint8_t ib_tx_enable;  /**< `HSIO_RGMII_CFG.IB_TX_ENA` */
  uint8_t rgmii_rx_rst;  /**< `HSIO_RGMII_CFG.RGMII_RX_RST` */
  uint8_t rgmii_tx_rst;  /**< `HSIO_RGMII_CFG.RGMII_TX_RST` */
  uint8_t tx_clk_cfg;    /**< `HSIO_RGMII_CFG.TX_CLK_CFG` */
  uint8_t link_up;       /**< `HSIO_RGMII_CFG.IB_RX_LINK_STATUS` */
  uint8_t duplex;        /**< `HSIO_RGMII_CFG.IB_RX_DUPLEX` */
  uint8_t speed_code;    /**< `HSIO_RGMII_CFG.IB_RX_SPEED` */
} lan9645_phylink_rgmii_cfg_t;

/**
 * @brief One CPU-port MAC/admin snapshot used by phylink control.
 */
typedef struct {
  uint8_t port;          /**< Switch CPU port index. */
  uint8_t admin_up;      /**< Administrative forwarding state. */
  uint16_t speed_mbps;   /**< Target speed (`10` or `100`). */
  uint8_t full_duplex;   /**< Duplex mode (`1=full`, `0=half`). */
  uint8_t mac_rx_ena;    /**< MAC RX enable state. */
  uint8_t mac_tx_ena;    /**< MAC TX enable state. */
} lan9645_phylink_cpu_cfg_t;

/**
 * @brief Initializes phylink baseline service state.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_phylink_init(void);

/**
 * @brief Reads one RGMII interface configuration/status snapshot.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] rgmii_idx RGMII interface index.
 * @param[out] out Destination RGMII snapshot.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_phylink_get_rgmii_cfg(
  lan9645_core_t *core,
  uint8_t rgmii_idx,
  lan9645_phylink_rgmii_cfg_t *out
);

/**
 * @brief Writes one RGMII interface configuration with readback verification.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] rgmii_idx RGMII interface index.
 * @param[in] ib_enable New in-band enable bit.
 * @param[in] ib_rx_enable New RX in-band enable bit.
 * @param[in] ib_tx_enable New TX in-band enable bit.
 * @param[in] tx_clk_cfg New TX clock selector.
 * @param[in] rgmii_rx_rst New RX reset bit.
 * @param[in] rgmii_tx_rst New TX reset bit.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_phylink_set_rgmii_cfg(
  lan9645_core_t *core,
  uint8_t rgmii_idx,
  uint8_t ib_enable,
  uint8_t ib_rx_enable,
  uint8_t ib_tx_enable,
  uint8_t tx_clk_cfg,
  uint8_t rgmii_rx_rst,
  uint8_t rgmii_tx_rst
);

/**
 * @brief Reads one CPU-port MAC/admin snapshot.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] cpu_port CPU port index.
 * @param[out] out Destination CPU-port snapshot.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_phylink_get_cpu_cfg(
  lan9645_core_t *core,
  uint8_t cpu_port,
  lan9645_phylink_cpu_cfg_t *out
);

/**
 * @brief Writes one CPU-port MAC/admin configuration with readback verification.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] cpu_port CPU port index.
 * @param[in] admin_up New admin state.
 * @param[in] speed_mbps New target speed (`10` or `100`).
 * @param[in] full_duplex New duplex mode.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_phylink_set_cpu_cfg(
  lan9645_core_t *core,
  uint8_t cpu_port,
  uint8_t admin_up,
  uint16_t speed_mbps,
  uint8_t full_duplex
);

/**
 * @brief Applies deterministic phylink baseline for the RGMII1 CPU path.
 *
 * Baseline policy:
 * - enforce CPU port `8` administrative up and `100M/full` MAC mode
 * - enable RGMII1 in-band status lanes (`IB_ENA`, `IB_RX_ENA`, `IB_TX_ENA`)
 * - clear RGMII RX/TX reset bits
 * - verify RGMII1 status sampling path is readable
 *
 * @param[in,out] core Initialized LAN9645 core context.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_phylink_apply_baseline(lan9645_core_t *core);

#ifdef __cplusplus
}
#endif

#endif /* LAN9645_PHYLINK_H */
