#ifndef LAN9645_USER_PHY_ADAPTER_H
#define LAN9645_USER_PHY_ADAPTER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "app_common_types.h"
#include "lan9645/lan9645_core.h"

#define LAN9645_USER_PHY_PORT0          (0u)
#define LAN9645_USER_PHY_PORT1          (1u)
#define LAN9645_USER_PHY_PORT_COUNT     (2u)

#define LAN9645_USER_PHY_REG_BCR        (0x0000u)
#define LAN9645_USER_PHY_REG_BSR        (0x0001u)
#define LAN9645_USER_PHY_REG_PHYI1R     (0x0002u)
#define LAN9645_USER_PHY_REG_PHYI2R     (0x0003u)
#define LAN9645_USER_PHY_REG_ANAR       (0x0004u)
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

typedef struct {
  lan9645_core_t *core;
  uint8_t initialized;
  uint8_t active_port;
  uint8_t autoneg_enabled[LAN9645_USER_PHY_PORT_COUNT];
  uint8_t force_speed_100[LAN9645_USER_PHY_PORT_COUNT];
  uint8_t force_full_duplex[LAN9645_USER_PHY_PORT_COUNT];
  uint8_t loopback_enabled[LAN9645_USER_PHY_PORT_COUNT];
  uint16_t control_shadow[LAN9645_USER_PHY_PORT_COUNT];
  uint16_t anar_shadow[LAN9645_USER_PHY_PORT_COUNT];
  uint16_t anlpar_shadow[LAN9645_USER_PHY_PORT_COUNT];
  uint16_t phy_id1;
  uint16_t phy_id2;
} lan9645_user_phy_adapter_t;

app_status_t lan9645_user_phy_adapter_init(
  lan9645_user_phy_adapter_t *ctx,
  lan9645_core_t *core
);
app_status_t lan9645_user_phy_adapter_bind(lan9645_user_phy_adapter_t *ctx);
app_status_t lan9645_user_phy_adapter_unbind(void);
app_status_t lan9645_user_phy_adapter_set_active_port(lan9645_user_phy_adapter_t *ctx, uint8_t port);
app_status_t lan9645_user_phy_adapter_get_active_port(
  const lan9645_user_phy_adapter_t *ctx,
  uint8_t *port
);
app_status_t lan9645_user_phy_adapter_read_reg(
  lan9645_user_phy_adapter_t *ctx,
  uint32_t phy_addr,
  uint32_t reg_addr,
  uint32_t *value
);
app_status_t lan9645_user_phy_adapter_write_reg(
  lan9645_user_phy_adapter_t *ctx,
  uint32_t phy_addr,
  uint32_t reg_addr,
  uint32_t value
);
app_status_t lan9645_user_phy_adapter_get_link_state(
  lan9645_user_phy_adapter_t *ctx,
  uint8_t port,
  int32_t *link_state
);

int32_t lan9645_user_phy_adapter_io_read_reg(
  uint32_t phy_addr,
  uint32_t reg_addr,
  uint32_t *value
);
int32_t lan9645_user_phy_adapter_io_write_reg(
  uint32_t phy_addr,
  uint32_t reg_addr,
  uint32_t value
);
int32_t lan9645_user_phy_adapter_io_get_tick(void);

#ifdef __cplusplus
}
#endif

#endif /* LAN9645_USER_PHY_ADAPTER_H */
