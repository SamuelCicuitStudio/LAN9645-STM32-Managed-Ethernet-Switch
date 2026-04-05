#ifndef LAN9645_USER_PHY_ADAPTER_H
#define LAN9645_USER_PHY_ADAPTER_H

/**
 * @file lan9645_user_phy_adapter.h
 * @brief USER_PHY bridge that lets STM32Cube HAL ETH query the LAN9645 copper PHYs.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "app_common_types.h"
#include "lan9645/lan9645_core.h"

/** @brief Logical USER_PHY index for copper port 0. */
#define LAN9645_USER_PHY_PORT0          (0u)
/** @brief Logical USER_PHY index for copper port 1. */
#define LAN9645_USER_PHY_PORT1          (1u)
/** @brief Number of copper PHY ports exposed through the USER_PHY adapter. */
#define LAN9645_USER_PHY_PORT_COUNT     (2u)

/** @brief Clause-22 basic control register address. */
#define LAN9645_USER_PHY_REG_BCR        (0x0000u)
/** @brief Clause-22 basic status register address. */
#define LAN9645_USER_PHY_REG_BSR        (0x0001u)
/** @brief Clause-22 PHY identifier register 1 address. */
#define LAN9645_USER_PHY_REG_PHYI1R     (0x0002u)
/** @brief Clause-22 PHY identifier register 2 address. */
#define LAN9645_USER_PHY_REG_PHYI2R     (0x0003u)
/** @brief Clause-22 auto-negotiation advertisement register address. */
#define LAN9645_USER_PHY_REG_ANAR       (0x0004u)
/** @brief Clause-22 link partner ability register address. */
#define LAN9645_USER_PHY_REG_ANLPAR     (0x0005u)

#define LAN9645_USER_PHY_BCR_SOFT_RESET       (0x8000u)
#define LAN9645_USER_PHY_BCR_LOOPBACK         (0x4000u)
#define LAN9645_USER_PHY_BCR_SPEED_SELECT     (0x2000u)
#define LAN9645_USER_PHY_BCR_AUTONEGO_EN      (0x1000u)
#define LAN9645_USER_PHY_BCR_POWER_DOWN       (0x0800u)
#define LAN9645_USER_PHY_BCR_ISOLATE          (0x0400u)
#define LAN9645_USER_PHY_BCR_RESTART_AUTONEGO (0x0200u)
#define LAN9645_USER_PHY_BCR_DUPLEX_MODE      (0x0100u)

#define LAN9645_USER_PHY_BSR_100BASE_TX_FD    (0x4000u)
#define LAN9645_USER_PHY_BSR_100BASE_TX_HD    (0x2000u)
#define LAN9645_USER_PHY_BSR_10BASE_T_FD      (0x1000u)
#define LAN9645_USER_PHY_BSR_10BASE_T_HD      (0x0800u)
#define LAN9645_USER_PHY_BSR_EXTENDED_STATUS  (0x0100u)
#define LAN9645_USER_PHY_BSR_AUTONEGO_CPLT    (0x0020u)
#define LAN9645_USER_PHY_BSR_AUTONEGO_ABILITY (0x0008u)
#define LAN9645_USER_PHY_BSR_LINK_STATUS      (0x0004u)
#define LAN9645_USER_PHY_BSR_EXTENDED_CAP     (0x0001u)

#define LAN9645_USER_PHY_ANAR_NEXT_PAGE            (0x8000u)
#define LAN9645_USER_PHY_ANAR_REMOTE_FAULT         (0x2000u)
#define LAN9645_USER_PHY_ANAR_PAUSE_OPERATION      (0x0C00u)
#define LAN9645_USER_PHY_ANAR_100BASE_TX_FD        (0x0100u)
#define LAN9645_USER_PHY_ANAR_100BASE_TX           (0x0080u)
#define LAN9645_USER_PHY_ANAR_10BASE_T_FD          (0x0040u)
#define LAN9645_USER_PHY_ANAR_10BASE_T             (0x0020u)
#define LAN9645_USER_PHY_ANAR_SELECTOR_FIELD       (0x000Fu)

#define LAN9645_USER_PHY_LINK_DOWN             (1)
#define LAN9645_USER_PHY_100MBITS_FULLDUPLEX  (2)
#define LAN9645_USER_PHY_100MBITS_HALFDUPLEX  (3)
#define LAN9645_USER_PHY_10MBITS_FULLDUPLEX   (4)
#define LAN9645_USER_PHY_10MBITS_HALFDUPLEX   (5)
#define LAN9645_USER_PHY_AUTONEGO_NOTDONE     (6)

/**
 * @brief USER_PHY adapter state backing the CubeMX-generated custom PHY hooks.
 */
typedef struct {
  lan9645_core_t *core; /**< LAN9645 core helper used to reach internal PHY control. */
  uint8_t initialized;  /**< Set after adapter initialization. */
  uint8_t active_port;  /**< Selected copper port exposed to the Cube HAL PHY logic. */
  uint8_t autoneg_enabled[LAN9645_USER_PHY_PORT_COUNT];    /**< Cached autoneg enable state per port. */
  uint8_t force_speed_100[LAN9645_USER_PHY_PORT_COUNT];    /**< Cached forced-speed selection per port. */
  uint8_t force_full_duplex[LAN9645_USER_PHY_PORT_COUNT];  /**< Cached duplex selection per port. */
  uint8_t loopback_enabled[LAN9645_USER_PHY_PORT_COUNT];   /**< Cached loopback state per port. */
  uint16_t control_shadow[LAN9645_USER_PHY_PORT_COUNT];    /**< Software mirror of BCR per port. */
  uint16_t anar_shadow[LAN9645_USER_PHY_PORT_COUNT];       /**< Software mirror of ANAR per port. */
  uint16_t anlpar_shadow[LAN9645_USER_PHY_PORT_COUNT];     /**< Software mirror of ANLPAR per port. */
  uint16_t phy_id1;   /**< Identifier value returned through PHYI1R. */
  uint16_t phy_id2;   /**< Identifier value returned through PHYI2R. */
} lan9645_user_phy_adapter_t;

/**
 * @brief Initializes the USER_PHY adapter.
 *
 * @param[out] ctx Adapter context to initialize.
 * @param[in] core LAN9645 core helper used for low-level access.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_user_phy_adapter_init(
  lan9645_user_phy_adapter_t *ctx,
  lan9645_core_t *core
);

/**
 * @brief Installs the adapter as the active global USER_PHY backend.
 *
 * @param[in] ctx Adapter context to bind.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_user_phy_adapter_bind(lan9645_user_phy_adapter_t *ctx);

/**
 * @brief Removes the globally bound USER_PHY adapter.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_user_phy_adapter_unbind(void);

/**
 * @brief Selects which LAN9645 copper port is exposed to the USER_PHY logic.
 *
 * @param[in,out] ctx Adapter context.
 * @param[in] port Port index, typically @ref LAN9645_USER_PHY_PORT0 or @ref LAN9645_USER_PHY_PORT1.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_user_phy_adapter_set_active_port(lan9645_user_phy_adapter_t *ctx, uint8_t port);

/**
 * @brief Returns the currently selected USER_PHY copper port.
 *
 * @param[in] ctx Adapter context.
 * @param[out] port Destination for the current active port.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_user_phy_adapter_get_active_port(
  const lan9645_user_phy_adapter_t *ctx,
  uint8_t *port
);

/**
 * @brief Reads one PHY register through the USER_PHY abstraction.
 *
 * @param[in,out] ctx Adapter context.
 * @param[in] phy_addr Requested PHY address from the caller.
 * @param[in] reg_addr Clause-22 register address.
 * @param[out] value Destination for the register value.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_user_phy_adapter_read_reg(
  lan9645_user_phy_adapter_t *ctx,
  uint32_t phy_addr,
  uint32_t reg_addr,
  uint32_t *value
);

/**
 * @brief Writes one PHY register through the USER_PHY abstraction.
 *
 * @param[in,out] ctx Adapter context.
 * @param[in] phy_addr Requested PHY address from the caller.
 * @param[in] reg_addr Clause-22 register address.
 * @param[in] value Register value to write.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_user_phy_adapter_write_reg(
  lan9645_user_phy_adapter_t *ctx,
  uint32_t phy_addr,
  uint32_t reg_addr,
  uint32_t value
);

/**
 * @brief Returns a simplified link-state code expected by the ETH middleware.
 *
 * @param[in,out] ctx Adapter context.
 * @param[in] port Copper port to sample.
 * @param[out] link_state Destination for one of the @c LAN9645_USER_PHY_* state codes.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_user_phy_adapter_get_link_state(
  lan9645_user_phy_adapter_t *ctx,
  uint8_t port,
  int32_t *link_state
);

/**
 * @brief Global callback used by the Cube-generated USER_PHY layer to read a PHY register.
 *
 * @param[in] phy_addr Requested PHY address.
 * @param[in] reg_addr Clause-22 register address.
 * @param[out] value Destination for the register value.
 *
 * @return 0 on success, negative error code on failure.
 */
int32_t lan9645_user_phy_adapter_io_read_reg(
  uint32_t phy_addr,
  uint32_t reg_addr,
  uint32_t *value
);

/**
 * @brief Global callback used by the Cube-generated USER_PHY layer to write a PHY register.
 *
 * @param[in] phy_addr Requested PHY address.
 * @param[in] reg_addr Clause-22 register address.
 * @param[in] value Register value to write.
 *
 * @return 0 on success, negative error code on failure.
 */
int32_t lan9645_user_phy_adapter_io_write_reg(
  uint32_t phy_addr,
  uint32_t reg_addr,
  uint32_t value
);

/**
 * @brief Global callback used by the Cube-generated USER_PHY layer to get the current tick.
 *
 * @return Current millisecond tick count cast to signed 32-bit integer.
 */
int32_t lan9645_user_phy_adapter_io_get_tick(void);

#ifdef __cplusplus
}
#endif

#endif /* LAN9645_USER_PHY_ADAPTER_H */
