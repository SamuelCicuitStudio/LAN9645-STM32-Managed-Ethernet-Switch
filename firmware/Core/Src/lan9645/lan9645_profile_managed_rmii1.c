#include "lan9645/lan9645_profile_managed_rmii1.h"

/**
 * @file lan9645_profile_managed_rmii1.c
 * @brief Fixed bring-up profile for the current managed LAN9645 RMII design.
 */

#include <stddef.h>

#include "lan9645/lan9645_reg.h"
#include "lan9645/lan9645_regs.h"

#define LAN9645_PROFILE_DEV_CUPHY0   (0u)
#define LAN9645_PROFILE_DEV_CUPHY1   (1u)
#define LAN9645_PROFILE_DEV_CPU_RMII (8u)

#define LAN9645_PROFILE_LINK_SPEED_10    (1u)
#define LAN9645_PROFILE_LINK_SPEED_100   (2u)
#define LAN9645_PROFILE_LINK_SPEED_1000  (3u)
#define LAN9645_PROFILE_CPU_DEV_IRQ_ENABLE_MASK (0x1FFu)

/**
 * @brief Convenience wrapper that reads one descriptor from target instance 0.
 */
static app_status_t lan9645_profile_read_desc(
  lan9645_core_t *core,
  const lan9645_reg_desc_t *desc,
  uint32_t group_instance,
  uint32_t reg_instance,
  uint32_t *value
)
{
  if ((core == NULL) || (core->reg == NULL) || (desc == NULL) || (value == NULL)) {
    return APP_EINVAL;
  }

  return lan9645_reg_read32_desc(core->reg, desc, 0u, group_instance, reg_instance, value);
}

/**
 * @brief Reads back a register field and verifies it matches the expected masked value.
 */
static app_status_t lan9645_profile_verify_masked(
  lan9645_core_t *core,
  const lan9645_reg_desc_t *desc,
  uint32_t group_instance,
  uint32_t reg_instance,
  uint32_t mask,
  uint32_t expected
)
{
  uint32_t val = 0u;
  app_status_t st = lan9645_profile_read_desc(core, desc, group_instance, reg_instance, &val);
  if (st != APP_OK) {
    return st;
  }

  return (((val & mask) == (expected & mask)) ? APP_OK : APP_EIO);
}

/**
 * @brief Reads the feature-disable register twice to reject unstable SPI data.
 */
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

/**
 * @brief Configures one DEV MAC block for the requested speed and duplex.
 */
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

/**
 * @brief Configures the external CPU port routing used by the STM32.
 */
static app_status_t lan9645_profile_configure_ext_cpu(lan9645_core_t *core)
{
  uint32_t mask = 0u;
  uint32_t value = 0u;

  if ((core == NULL) || (core->reg == NULL)) {
    return APP_EINVAL;
  }

  mask =
    LAN9645_QSYS_EXT_CPU_CFG_EXT_CPU_PORT |
    LAN9645_QSYS_EXT_CPU_CFG_EXT_CPUQ_MSK |
    LAN9645_QSYS_EXT_CPU_CFG_EXT_CPU_KILL_ENA |
    LAN9645_QSYS_EXT_CPU_CFG_INT_CPU_KILL_ENA;

  value =
    LAN9645_QSYS_EXT_CPU_CFG_EXT_CPU_PORT_SET(LAN9645_PROFILE_DEV_CPU_RMII) |
    LAN9645_QSYS_EXT_CPU_CFG_EXT_CPUQ_MSK_SET(0xFFu) |
    LAN9645_QSYS_EXT_CPU_CFG_EXT_CPU_KILL_ENA |
    LAN9645_QSYS_EXT_CPU_CFG_INT_CPU_KILL_ENA;

  return lan9645_reg_rmw32_desc(
    core->reg,
    &LAN9645_REG_QSYS_EXT_CPU_CFG,
    0u,
    0u,
    0u,
    mask,
    value
  );
}

/**
 * @brief Enables the interrupt sources used by the current firmware.
 */
static app_status_t lan9645_profile_configure_irq(lan9645_core_t *core)
{
  app_status_t st = APP_OK;

  if ((core == NULL) || (core->reg == NULL)) {
    return APP_EINVAL;
  }

  st = lan9645_reg_rmw32_desc(
    core->reg,
    &LAN9645_REG_ANA_ANAINTR,
    0u,
    0u,
    0u,
    LAN9645_ANA_ANAINTR_INTR_ENA,
    LAN9645_ANA_ANAINTR_INTR_ENA
  );
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_reg_rmw32_desc(
    core->reg,
    &LAN9645_REG_CPU_DEV_INTR_ENA,
    0u,
    0u,
    0u,
    LAN9645_CPU_DEV_INTR_MASK,
    LAN9645_PROFILE_CPU_DEV_IRQ_ENABLE_MASK
  );
  if (st != APP_OK) {
    return st;
  }

  return APP_OK;
}

/**
 * @brief Verifies that one DEV MAC block matches the intended 10/100 profile.
 */
static app_status_t lan9645_profile_verify_dev_mac(
  lan9645_core_t *core,
  uint32_t dev,
  uint32_t expected_link_speed,
  uint8_t expected_full_duplex
)
{
  uint32_t speed_expected = LAN9645_DEV_CLOCK_CFG_LINK_SPEED_SET(expected_link_speed);
  uint32_t mode_expected = (expected_full_duplex != 0u) ? LAN9645_DEV_MAC_MODE_CFG_FDX_ENA : 0u;
  app_status_t st = APP_OK;

  st = lan9645_profile_verify_masked(
    core,
    &LAN9645_REG_DEV_CLOCK_CFG,
    dev,
    0u,
    LAN9645_DEV_CLOCK_CFG_LINK_SPEED |
      LAN9645_DEV_CLOCK_CFG_MAC_TX_RST |
      LAN9645_DEV_CLOCK_CFG_MAC_RX_RST |
      LAN9645_DEV_CLOCK_CFG_PCS_TX_RST |
      LAN9645_DEV_CLOCK_CFG_PCS_RX_RST |
      LAN9645_DEV_CLOCK_CFG_PORT_RST |
      LAN9645_DEV_CLOCK_CFG_PHY_RST,
    speed_expected
  );
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_profile_verify_masked(
    core,
    &LAN9645_REG_DEV_MAC_MODE_CFG,
    dev,
    0u,
    LAN9645_DEV_MAC_MODE_CFG_GIGA_MODE_ENA | LAN9645_DEV_MAC_MODE_CFG_FDX_ENA,
    mode_expected
  );
  if (st != APP_OK) {
    return st;
  }

  return lan9645_profile_verify_masked(
    core,
    &LAN9645_REG_DEV_MAC_ENA_CFG,
    dev,
    0u,
    LAN9645_DEV_MAC_ENA_CFG_TX_ENA | LAN9645_DEV_MAC_ENA_CFG_RX_ENA,
    LAN9645_DEV_MAC_ENA_CFG_TX_ENA | LAN9645_DEV_MAC_ENA_CFG_RX_ENA
  );
}

/**
 * @brief Verifies that a switch port is enabled in QSYS.
 */
static app_status_t lan9645_profile_verify_port_enabled(lan9645_core_t *core, uint32_t port)
{
  return lan9645_profile_verify_masked(
    core,
    &LAN9645_REG_QSYS_SW_PORT_MODE,
    0u,
    port,
    LAN9645_QSYS_SW_PORT_MODE_PORT_ENA,
    LAN9645_QSYS_SW_PORT_MODE_PORT_ENA
  );
}

/**
 * @brief Verifies the interrupt configuration applied by the profile.
 */
static app_status_t lan9645_profile_verify_irq(lan9645_core_t *core)
{
  app_status_t st = APP_OK;

  st = lan9645_profile_verify_masked(
    core,
    &LAN9645_REG_ANA_ANAINTR,
    0u,
    0u,
    LAN9645_ANA_ANAINTR_INTR_ENA,
    LAN9645_ANA_ANAINTR_INTR_ENA
  );
  if (st != APP_OK) {
    return st;
  }

  return lan9645_profile_verify_masked(
    core,
    &LAN9645_REG_CPU_DEV_INTR_ENA,
    0u,
    0u,
    LAN9645_CPU_DEV_INTR_MASK,
    LAN9645_PROFILE_CPU_DEV_IRQ_ENABLE_MASK
  );
}

/**
 * @brief Applies and verifies the project's fixed LAN9645 managed-switch profile.
 *
 * @param[in,out] core Core helper used to reach the switch.
 * @param[out] out Optional summary of the resulting configuration.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_profile_managed_rmii1_apply(
  lan9645_core_t *core,
  lan9645_profile_result_t *out
)
{
  uint32_t chip_id = 0u;
  uint32_t feat_disable = 0u;
  uint32_t ext_cpu_cfg = 0u;
  uint32_t ports_enabled_mask = 0u;
  app_status_t st = APP_OK;

  if (core == NULL) {
    return APP_EINVAL;
  }
  if ((core->initialized == 0u) || (core->reg == NULL)) {
    return APP_ESTATE;
  }

  st = lan9645_core_read_chip_id(core, &chip_id);
  if (st != APP_OK) {
    return st;
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

  st = lan9645_profile_configure_irq(core);
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

  st = lan9645_profile_read_desc(core, &LAN9645_REG_QSYS_EXT_CPU_CFG, 0u, 0u, &ext_cpu_cfg);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_profile_verify_masked(
    core,
    &LAN9645_REG_QSYS_EXT_CPU_CFG,
    0u,
    0u,
    LAN9645_QSYS_EXT_CPU_CFG_EXT_CPU_PORT |
      LAN9645_QSYS_EXT_CPU_CFG_EXT_CPUQ_MSK |
      LAN9645_QSYS_EXT_CPU_CFG_EXT_CPU_KILL_ENA |
      LAN9645_QSYS_EXT_CPU_CFG_INT_CPU_KILL_ENA,
    LAN9645_QSYS_EXT_CPU_CFG_EXT_CPU_PORT_SET(LAN9645_PROFILE_DEV_CPU_RMII) |
      LAN9645_QSYS_EXT_CPU_CFG_EXT_CPUQ_MSK_SET(0xFFu) |
      LAN9645_QSYS_EXT_CPU_CFG_EXT_CPU_KILL_ENA |
      LAN9645_QSYS_EXT_CPU_CFG_INT_CPU_KILL_ENA
  );
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_profile_verify_irq(core);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_profile_verify_dev_mac(core, LAN9645_PROFILE_DEV_CUPHY0, LAN9645_PROFILE_LINK_SPEED_100, 1u);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_profile_verify_dev_mac(core, LAN9645_PROFILE_DEV_CUPHY1, LAN9645_PROFILE_LINK_SPEED_100, 1u);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_profile_verify_dev_mac(core, LAN9645_PROFILE_DEV_CPU_RMII, LAN9645_PROFILE_LINK_SPEED_100, 1u);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_profile_verify_port_enabled(core, LAN9645_PROFILE_DEV_CUPHY0);
  if (st != APP_OK) {
    return st;
  }
  ports_enabled_mask |= (1u << LAN9645_PROFILE_DEV_CUPHY0);

  st = lan9645_profile_verify_port_enabled(core, LAN9645_PROFILE_DEV_CUPHY1);
  if (st != APP_OK) {
    return st;
  }
  ports_enabled_mask |= (1u << LAN9645_PROFILE_DEV_CUPHY1);

  st = lan9645_profile_verify_port_enabled(core, LAN9645_PROFILE_DEV_CPU_RMII);
  if (st != APP_OK) {
    return st;
  }
  ports_enabled_mask |= (1u << LAN9645_PROFILE_DEV_CPU_RMII);

  if (out != NULL) {
    out->chip_id = chip_id;
    out->feat_disable = feat_disable;
    out->ext_cpu_cfg = ext_cpu_cfg;
    out->ports_enabled_mask = ports_enabled_mask;
  }

  return APP_OK;
}
