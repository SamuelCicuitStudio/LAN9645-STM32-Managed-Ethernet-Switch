#include "lan9645/lan9645_profile_managed_rmii1.h"

#include <stddef.h>

#include "lan9645/lan9645_reg.h"
#include "lan9645/lan9645_regs.h"

#define LAN9645_PROFILE_DEV_CUPHY0   (0u)
#define LAN9645_PROFILE_DEV_CUPHY1   (1u)
#define LAN9645_PROFILE_DEV_CPU_RMII (8u)

#define LAN9645_PROFILE_LINK_SPEED_10    (1u)
#define LAN9645_PROFILE_LINK_SPEED_100   (2u)
#define LAN9645_PROFILE_LINK_SPEED_1000  (3u)

static app_status_t lan9645_profile_read_feat_disable(
  lan9645_core_t *core,
  uint32_t *feat_disable
)
{
  uint32_t read0 = 0u;
  uint32_t read1 = 0u;
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->reg == NULL) || (feat_disable == NULL)) {
    return APP_EINVAL;
  }

  st = lan9645_reg_read32_desc(core->reg, &LAN9645_REG_GCB_FEAT_DISABLE, 0u, 0u, 0u, &read0);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_reg_read32_desc(core->reg, &LAN9645_REG_GCB_FEAT_DISABLE, 0u, 0u, 0u, &read1);
  if (st != APP_OK) {
    return st;
  }

  if (read0 != read1) {
    return APP_EIO;
  }

  *feat_disable = read1;
  return APP_OK;
}

static app_status_t lan9645_profile_configure_dev_mac(
  lan9645_core_t *core,
  uint32_t dev,
  uint32_t link_speed,
  uint8_t full_duplex
)
{
  uint32_t clock_mask = 0u;
  uint32_t clock_val = 0u;
  uint32_t mode_mask = 0u;
  uint32_t mode_val = 0u;

  if ((core == NULL) || (core->reg == NULL)) {
    return APP_EINVAL;
  }

  clock_mask =
    LAN9645_DEV_CLOCK_CFG_MAC_TX_RST |
    LAN9645_DEV_CLOCK_CFG_MAC_RX_RST |
    LAN9645_DEV_CLOCK_CFG_PCS_TX_RST |
    LAN9645_DEV_CLOCK_CFG_PCS_RX_RST |
    LAN9645_DEV_CLOCK_CFG_PORT_RST |
    LAN9645_DEV_CLOCK_CFG_PHY_RST |
    LAN9645_DEV_CLOCK_CFG_LINK_SPEED;

  clock_val = LAN9645_DEV_CLOCK_CFG_LINK_SPEED_SET(link_speed);

  if (lan9645_reg_rmw32_desc(
        core->reg,
        &LAN9645_REG_DEV_CLOCK_CFG,
        0u,
        dev,
        0u,
        clock_mask,
        clock_val) != APP_OK) {
    return APP_EIO;
  }

  mode_mask = LAN9645_DEV_MAC_MODE_CFG_GIGA_MODE_ENA | LAN9645_DEV_MAC_MODE_CFG_FDX_ENA;
  mode_val = (full_duplex != 0u) ? LAN9645_DEV_MAC_MODE_CFG_FDX_ENA : 0u;

  if (lan9645_reg_rmw32_desc(
        core->reg,
        &LAN9645_REG_DEV_MAC_MODE_CFG,
        0u,
        dev,
        0u,
        mode_mask,
        mode_val) != APP_OK) {
    return APP_EIO;
  }

  if (lan9645_reg_rmw32_desc(
        core->reg,
        &LAN9645_REG_DEV_MAC_ENA_CFG,
        0u,
        dev,
        0u,
        LAN9645_DEV_MAC_ENA_CFG_TX_ENA | LAN9645_DEV_MAC_ENA_CFG_RX_ENA,
        LAN9645_DEV_MAC_ENA_CFG_TX_ENA | LAN9645_DEV_MAC_ENA_CFG_RX_ENA) != APP_OK) {
    return APP_EIO;
  }

  return APP_OK;
}

static app_status_t lan9645_profile_configure_ext_cpu(lan9645_core_t *core)
{
  if ((core == NULL) || (core->reg == NULL)) {
    return APP_EINVAL;
  }

  return lan9645_reg_rmw32_desc(
    core->reg,
    &LAN9645_REG_QSYS_EXT_CPU_CFG,
    0u,
    0u,
    0u,
    LAN9645_QSYS_EXT_CPU_CFG_EXT_CPU_PORT | LAN9645_QSYS_EXT_CPU_CFG_EXT_CPUQ_MSK,
    LAN9645_QSYS_EXT_CPU_CFG_EXT_CPU_PORT_SET(LAN9645_PROFILE_DEV_CPU_RMII) |
      LAN9645_QSYS_EXT_CPU_CFG_EXT_CPUQ_MSK_SET(0xFFu)
  );
}

app_status_t lan9645_profile_managed_rmii1_apply(
  lan9645_core_t *core,
  lan9645_profile_result_t *out
)
{
  uint32_t feat_disable = 0u;
  app_status_t st = APP_OK;

  if (core == NULL) {
    return APP_EINVAL;
  }
  if ((core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }

  st = lan9645_core_soft_reset(core, 0u);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_profile_read_feat_disable(core, &feat_disable);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_profile_configure_ext_cpu(core);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_profile_configure_dev_mac(core, LAN9645_PROFILE_DEV_CUPHY0, LAN9645_PROFILE_LINK_SPEED_100, 1u);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_profile_configure_dev_mac(core, LAN9645_PROFILE_DEV_CUPHY1, LAN9645_PROFILE_LINK_SPEED_100, 1u);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_profile_configure_dev_mac(core, LAN9645_PROFILE_DEV_CPU_RMII, LAN9645_PROFILE_LINK_SPEED_100, 1u);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_core_set_port_enable(core, LAN9645_PROFILE_DEV_CUPHY0, 1u);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_core_set_port_enable(core, LAN9645_PROFILE_DEV_CUPHY1, 1u);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_core_set_port_enable(core, LAN9645_PROFILE_DEV_CPU_RMII, 1u);
  if (st != APP_OK) {
    return st;
  }

  if (out != NULL) {
    out->feat_disable = feat_disable;
  }

  return APP_OK;
}
