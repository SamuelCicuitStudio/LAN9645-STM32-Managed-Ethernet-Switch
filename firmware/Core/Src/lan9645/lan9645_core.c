#include "lan9645/lan9645_core.h"

/**
 * @file lan9645_core.c
 * @brief High-level LAN9645 core helpers for reset, SPI setup, and IRQ snapshots.
 */

#include <stddef.h>
#include <string.h>

#include "diag/diag_core.h"
#include "diag/diag_router.h"
#include "lan9645/lan9645_spi.h"

#define LAN9645_SPI_BYTE_ORDER_LE      (0x00000000u)

#define LAN9645_CORE_SPI_SETUP_DELAY_MS (5u)
#define LAN9645_CORE_MAX_DEV_PORT       (8u)
#define LAN9645_CORE_SPEED_CODE_10      (1u)
#define LAN9645_CORE_SPEED_CODE_100     (2u)

#define LAN9645_CORE_PORT_STEP_ADMIN_SET        (1u)
#define LAN9645_CORE_PORT_STEP_ADMIN_VERIFY     (2u)
#define LAN9645_CORE_PORT_STEP_MODE_CLOCK_SET   (3u)
#define LAN9645_CORE_PORT_STEP_MODE_CLOCK_VERIFY (4u)
#define LAN9645_CORE_PORT_STEP_MODE_MAC_SET     (5u)
#define LAN9645_CORE_PORT_STEP_MODE_MAC_VERIFY  (6u)
#define LAN9645_CORE_PORT_STEP_MODE_ENA_SET     (7u)
#define LAN9645_CORE_PORT_STEP_MODE_ENA_VERIFY  (8u)

/**
 * @brief Validates that the LAN9645 core context is ready.
 *
 * @param[in] ctx Core context to validate.
 *
 * @return @ref APP_OK when the context is usable.
 */
static app_status_t lan9645_core_check_ctx(const lan9645_core_t *ctx)
{
  if (ctx == NULL) {
    return APP_EINVAL;
  }
  if ((ctx->initialized == 0u) || (ctx->reg == NULL)) {
    return APP_ESTATE;
  }
  return APP_OK;
}

/**
 * @brief Reports one port-configuration failure into diagnostics and routed serial logs.
 *
 * @param[in] port Port index associated with the failure.
 * @param[in] reg_addr Register-relative address tag.
 * @param[in] st Failure status code.
 * @param[in] step Internal step tag for deterministic troubleshooting.
 */
static void lan9645_core_report_port_cfg_fail(uint8_t port, uint32_t reg_addr, app_status_t st, uint32_t step)
{
  diag_event_t evt = {0};

  evt.id = DIAG_EVT_LAN_PORT_CFG_FAIL;
  evt.severity = DIAG_SEV_ERR;
  evt.dev = (uint32_t)port;
  evt.reg = reg_addr;
  evt.arg0 = (uint32_t)(int32_t)st;
  evt.arg1 = step;

  (void)diag_core_report(&evt);
  (void)diag_router_write_event(&evt);
}

/**
 * @brief Converts a requested 10/100 speed value into the DEV clock speed code.
 *
 * @param[in] speed_mbps Requested speed in Mbit/s.
 * @param[out] speed_code Destination speed code for `DEV_CLOCK_CFG.LINK_SPEED`.
 *
 * @return @ref APP_OK on success.
 */
static app_status_t lan9645_core_speed_to_code(uint16_t speed_mbps, uint32_t *speed_code)
{
  if (speed_code == NULL) {
    return APP_EINVAL;
  }

  if (speed_mbps == LAN9645_CORE_PORT_SPEED_10_MBPS) {
    *speed_code = LAN9645_CORE_SPEED_CODE_10;
    return APP_OK;
  }
  if (speed_mbps == LAN9645_CORE_PORT_SPEED_100_MBPS) {
    *speed_code = LAN9645_CORE_SPEED_CODE_100;
    return APP_OK;
  }

  return APP_EINVAL;
}

/**
 * @brief Decodes a DEV clock speed code into a user-facing Mbit/s value.
 *
 * @param[in] speed_code Encoded `DEV_CLOCK_CFG.LINK_SPEED` value.
 *
 * @return Speed in Mbit/s, or `0` when the code is not part of the 10/100 profile.
 */
static uint16_t lan9645_core_speed_from_code(uint32_t speed_code)
{
  if (speed_code == LAN9645_CORE_SPEED_CODE_10) {
    return LAN9645_CORE_PORT_SPEED_10_MBPS;
  }
  if (speed_code == LAN9645_CORE_SPEED_CODE_100) {
    return LAN9645_CORE_PORT_SPEED_100_MBPS;
  }
  return 0u;
}

/**
 * @brief Verifies one descriptor field with readback and reports mismatch through diagnostics.
 *
 * @param[in,out] ctx Core context.
 * @param[in] desc Register descriptor to read.
 * @param[in] group_instance Group instance index.
 * @param[in] reg_instance Register instance index.
 * @param[in] mask Field mask to verify.
 * @param[in] expected Expected masked value.
 * @param[in] port Port index tied to the verification.
 * @param[in] step Failure step tag used for diagnostics.
 *
 * @return @ref APP_OK on success.
 */
static app_status_t lan9645_core_verify_masked_desc(
  lan9645_core_t *ctx,
  const lan9645_reg_desc_t *desc,
  uint32_t group_instance,
  uint32_t reg_instance,
  uint32_t mask,
  uint32_t expected,
  uint8_t port,
  uint32_t step
)
{
  uint32_t value = 0u;
  uint32_t reg_tag = 0u;
  app_status_t st = APP_OK;

  reg_tag = lan9645_reg_desc_rel_addr(desc, group_instance, reg_instance);
  st = lan9645_reg_read32_desc(ctx->reg, desc, 0u, group_instance, reg_instance, &value);
  if (st != APP_OK) {
    lan9645_core_report_port_cfg_fail(port, reg_tag, st, step);
    return st;
  }

  if ((value & mask) != (expected & mask)) {
    lan9645_core_report_port_cfg_fail(port, reg_tag, APP_EIO, step);
    return APP_EIO;
  }

  return APP_OK;
}

/**
 * @brief Initializes the LAN9645 core helper.
 *
 * @param[out] ctx Core context to initialize.
 * @param[in] reg Register layer used to access the switch.
 * @param[in] rst_port GPIO port connected to LAN_RST.
 * @param[in] rst_pin GPIO pin connected to LAN_RST.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_core_init(
  lan9645_core_t *ctx,
  lan9645_reg_t *reg,
  GPIO_TypeDef *rst_port,
  uint16_t rst_pin
)
{
  if ((ctx == NULL) || (reg == NULL) || (rst_port == NULL) || (rst_pin == 0u)) {
    return APP_EINVAL;
  }

  memset(ctx, 0, sizeof(*ctx));
  ctx->reg = reg;
  ctx->rst_port = rst_port;
  ctx->rst_pin = rst_pin;
  ctx->spi_clock_hz = 0u;
  ctx->spi_if_ready = 0u;
  ctx->initialized = 1u;
  return APP_OK;
}

/**
 * @brief Pulses the external reset line and restores default SPI padding.
 *
 * @param[in,out] ctx Core context.
 * @param[in] assert_ms Reset-assertion duration.
 * @param[in] release_ms Post-release delay.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_core_hw_reset(lan9645_core_t *ctx, uint32_t assert_ms, uint32_t release_ms)
{
  app_status_t st = lan9645_core_check_ctx(ctx);
  if (st != APP_OK) {
    return st;
  }

  HAL_GPIO_WritePin(ctx->rst_port, ctx->rst_pin, GPIO_PIN_RESET);
  HAL_Delay(assert_ms);
  HAL_GPIO_WritePin(ctx->rst_port, ctx->rst_pin, GPIO_PIN_SET);
  HAL_Delay(release_ms);
  ctx->spi_if_ready = 0u;
  (void)lan9645_spi_set_padding(ctx->reg->spi, LAN9645_SPI_DEFAULT_PADDING);
  return APP_OK;
}

/**
 * @brief Programs the SPI interface configuration inside the LAN9645.
 *
 * @param[in,out] ctx Core context.
 * @param[in] spi_clock_hz Active STM32 SPI clock.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_core_spi_setup(lan9645_core_t *ctx, uint32_t spi_clock_hz)
{
  uint32_t if_cfgstat = 0u;
  uint8_t padding = 0u;
  app_status_t st = lan9645_core_check_ctx(ctx);
  if (st != APP_OK) {
    return st;
  }
  if (spi_clock_hz == 0u) {
    return APP_EINVAL;
  }

  st = lan9645_spi_calc_padding(spi_clock_hz, &padding);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_reg_write32_desc(
    ctx->reg,
    &LAN9645_REG_GCB_IF_CTRL,
    0u,
    0u,
    0u,
    LAN9645_SPI_BYTE_ORDER_LE
  );
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_reg_write32_desc(
    ctx->reg,
    &LAN9645_REG_GCB_IF_CFGSTAT,
    0u,
    0u,
    0u,
    LAN9645_GCB_IF_CFGSTAT_IF_CFG_SET((uint32_t)padding)
  );
  if (st != APP_OK) {
    return st;
  }

  HAL_Delay(LAN9645_CORE_SPI_SETUP_DELAY_MS);

  st = lan9645_reg_read32_desc(
    ctx->reg,
    &LAN9645_REG_GCB_IF_CFGSTAT,
    0u,
    0u,
    0u,
    &if_cfgstat
  );
  if (st != APP_OK) {
    return st;
  }
  if (LAN9645_FIELD_GET(LAN9645_GCB_IF_CFGSTAT_IF_CFG, if_cfgstat) != (uint32_t)padding) {
    return APP_EIO;
  }

  st = lan9645_spi_set_padding(ctx->reg->spi, padding);
  if (st != APP_OK) {
    return st;
  }

  ctx->spi_clock_hz = spi_clock_hz;
  ctx->spi_if_ready = 1u;
  return APP_OK;
}

/**
 * @brief Returns the current SPI interface status from both cached and hardware state.
 *
 * @param[in,out] ctx Core context.
 * @param[out] if_ctrl Optional IF_CTRL register snapshot.
 * @param[out] if_cfgstat Optional IF_CFGSTAT register snapshot.
 * @param[out] padding_bytes Optional cached padding byte count.
 * @param[out] spi_clock_hz Optional cached SPI clock.
 * @param[out] spi_if_ready Optional cached readiness flag.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_core_get_spi_status(
  lan9645_core_t *ctx,
  uint32_t *if_ctrl,
  uint32_t *if_cfgstat,
  uint8_t *padding_bytes,
  uint32_t *spi_clock_hz,
  uint8_t *spi_if_ready
)
{
  app_status_t st = lan9645_core_check_ctx(ctx);
  if (st != APP_OK) {
    return st;
  }

  if (padding_bytes != NULL) {
    *padding_bytes = ctx->reg->spi->padding_bytes;
  }
  if (spi_clock_hz != NULL) {
    *spi_clock_hz = ctx->spi_clock_hz;
  }
  if (spi_if_ready != NULL) {
    *spi_if_ready = ctx->spi_if_ready;
  }

  if ((if_ctrl == NULL) && (if_cfgstat == NULL)) {
    return APP_OK;
  }

  if (ctx->spi_if_ready == 0u) {
    return APP_ESTATE;
  }

  if (if_ctrl != NULL) {
    st = lan9645_reg_read32_desc(
      ctx->reg,
      &LAN9645_REG_GCB_IF_CTRL,
      0u,
      0u,
      0u,
      if_ctrl
    );
    if (st != APP_OK) {
      return st;
    }
  }

  if (if_cfgstat != NULL) {
    st = lan9645_reg_read32_desc(
      ctx->reg,
      &LAN9645_REG_GCB_IF_CFGSTAT,
      0u,
      0u,
      0u,
      if_cfgstat
    );
    if (st != APP_OK) {
      return st;
    }
  }

  return APP_OK;
}

/**
 * @brief Reads the switch chip identifier.
 *
 * @param[in,out] ctx Core context.
 * @param[out] chip_id Destination chip-ID value.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_core_read_chip_id(lan9645_core_t *ctx, uint32_t *chip_id)
{
  app_status_t st = lan9645_core_check_ctx(ctx);
  if (st != APP_OK) {
    return st;
  }
  if (chip_id == NULL) {
    return APP_EINVAL;
  }

  return lan9645_reg_read32_desc(
    ctx->reg,
    &LAN9645_REG_GCB_CHIP_ID,
    0u,
    0u,
    0u,
    chip_id
  );
}

/**
 * @brief Issues a software reset and waits until SPI access becomes valid again.
 *
 * @param[in,out] ctx Core context.
 * @param[in] timeout_ms Timeout in milliseconds, or 0 for default.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_core_soft_reset(lan9645_core_t *ctx, uint32_t timeout_ms)
{
  uint32_t start_ms = 0u;
  uint32_t now_ms = 0u;
  uint32_t val = 0u;
  uint32_t local_timeout = timeout_ms;
  app_status_t st = lan9645_core_check_ctx(ctx);
  if (st != APP_OK) {
    return st;
  }
  if (ctx->spi_if_ready == 0u) {
    return APP_ESTATE;
  }

  if (local_timeout == 0u) {
    local_timeout = LAN9645_CORE_SOFT_RESET_TIMEOUT_MS;
  }

  st = lan9645_reg_write32_desc(
    ctx->reg,
    &LAN9645_REG_GCB_SOFT_RST,
    0u,
    0u,
    0u,
    LAN9645_GCB_SOFT_RST_SOFT_SWC_RST
  );
  if (st != APP_OK) {
    return st;
  }

  ctx->spi_if_ready = 0u;
  (void)lan9645_spi_set_padding(ctx->reg->spi, LAN9645_SPI_DEFAULT_PADDING);

  start_ms = HAL_GetTick();
  now_ms = start_ms;
  while ((now_ms - start_ms) <= local_timeout) {
    st = lan9645_reg_read32_desc(
      ctx->reg,
      &LAN9645_REG_GCB_SOFT_RST,
      0u,
      0u,
      0u,
      &val
    );
    if ((st == APP_OK) && ((val & LAN9645_GCB_SOFT_RST_SOFT_SWC_RST) == 0u)) {
      return lan9645_core_spi_setup(ctx, ctx->spi_clock_hz);
    }
    HAL_Delay(1u);
    now_ms = HAL_GetTick();
  }

  return APP_ETIMEOUT;
}

/**
 * @brief Reads the feature-disable register latched by the switch straps.
 *
 * @param[in,out] ctx Core context.
 * @param[out] feat_disable Destination register value.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_core_read_feat_disable(lan9645_core_t *ctx, uint32_t *feat_disable)
{
  app_status_t st = lan9645_core_check_ctx(ctx);
  if (st != APP_OK) {
    return st;
  }
  if (feat_disable == NULL) {
    return APP_EINVAL;
  }

  return lan9645_reg_read32_desc(
    ctx->reg,
    &LAN9645_REG_GCB_FEAT_DISABLE,
    0u,
    0u,
    0u,
    feat_disable
  );
}

/**
 * @brief Sets the administrative forwarding state for one switch port.
 *
 * @param[in,out] ctx Core context.
 * @param[in] port Switch port index.
 * @param[in] up Set to 1 for administrative up.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_core_set_port_admin(lan9645_core_t *ctx, uint8_t port, uint8_t up)
{
  uint32_t value = (up != 0u) ? LAN9645_QSYS_SW_PORT_MODE_PORT_ENA : 0u;
  uint32_t reg_tag = lan9645_reg_desc_rel_addr(&LAN9645_REG_QSYS_SW_PORT_MODE, 0u, (uint32_t)port);
  app_status_t st = lan9645_core_check_ctx(ctx);
  if (st != APP_OK) {
    return st;
  }
  if (port > LAN9645_CORE_MAX_DEV_PORT) {
    return APP_EINVAL;
  }

  st = lan9645_reg_rmw32_desc(
    ctx->reg,
    &LAN9645_REG_QSYS_SW_PORT_MODE,
    0u,
    0u,
    (uint32_t)port,
    LAN9645_QSYS_SW_PORT_MODE_PORT_ENA,
    value
  );
  if (st != APP_OK) {
    lan9645_core_report_port_cfg_fail(port, reg_tag, st, LAN9645_CORE_PORT_STEP_ADMIN_SET);
    return st;
  }

  return lan9645_core_verify_masked_desc(
    ctx,
    &LAN9645_REG_QSYS_SW_PORT_MODE,
    0u,
    (uint32_t)port,
    LAN9645_QSYS_SW_PORT_MODE_PORT_ENA,
    value,
    port,
    LAN9645_CORE_PORT_STEP_ADMIN_VERIFY
  );
}

/**
 * @brief Applies 10/100 MAC mode configuration with readback verification.
 *
 * @param[in,out] ctx Core context.
 * @param[in] port Switch port index.
 * @param[in] speed_mbps Requested speed in Mbit/s.
 * @param[in] full_duplex Set to 1 for full duplex.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_core_set_port_mode(
  lan9645_core_t *ctx,
  uint8_t port,
  uint16_t speed_mbps,
  uint8_t full_duplex
)
{
  uint32_t speed_code = 0u;
  uint32_t clock_mask = 0u;
  uint32_t clock_value = 0u;
  uint32_t mode_mask = 0u;
  uint32_t mode_value = 0u;
  uint32_t ena_mask = 0u;
  uint32_t ena_value = 0u;
  app_status_t st = lan9645_core_check_ctx(ctx);
  if (st != APP_OK) {
    return st;
  }
  if (port > LAN9645_CORE_MAX_DEV_PORT) {
    return APP_EINVAL;
  }
  if ((full_duplex != 0u) && (full_duplex != 1u)) {
    return APP_EINVAL;
  }

  st = lan9645_core_speed_to_code(speed_mbps, &speed_code);
  if (st != APP_OK) {
    return st;
  }

  clock_mask =
    LAN9645_DEV_CLOCK_CFG_MAC_TX_RST |
    LAN9645_DEV_CLOCK_CFG_MAC_RX_RST |
    LAN9645_DEV_CLOCK_CFG_PCS_TX_RST |
    LAN9645_DEV_CLOCK_CFG_PCS_RX_RST |
    LAN9645_DEV_CLOCK_CFG_PORT_RST |
    LAN9645_DEV_CLOCK_CFG_PHY_RST |
    LAN9645_DEV_CLOCK_CFG_LINK_SPEED;
  clock_value = LAN9645_DEV_CLOCK_CFG_LINK_SPEED_SET(speed_code);

  st = lan9645_reg_rmw32_desc(
    ctx->reg,
    &LAN9645_REG_DEV_CLOCK_CFG,
    0u,
    (uint32_t)port,
    0u,
    clock_mask,
    clock_value
  );
  if (st != APP_OK) {
    lan9645_core_report_port_cfg_fail(
      port,
      lan9645_reg_desc_rel_addr(&LAN9645_REG_DEV_CLOCK_CFG, (uint32_t)port, 0u),
      st,
      LAN9645_CORE_PORT_STEP_MODE_CLOCK_SET
    );
    return st;
  }

  st = lan9645_core_verify_masked_desc(
    ctx,
    &LAN9645_REG_DEV_CLOCK_CFG,
    (uint32_t)port,
    0u,
    clock_mask,
    clock_value,
    port,
    LAN9645_CORE_PORT_STEP_MODE_CLOCK_VERIFY
  );
  if (st != APP_OK) {
    return st;
  }

  mode_mask = LAN9645_DEV_MAC_MODE_CFG_GIGA_MODE_ENA | LAN9645_DEV_MAC_MODE_CFG_FDX_ENA;
  mode_value = (full_duplex != 0u) ? LAN9645_DEV_MAC_MODE_CFG_FDX_ENA : 0u;

  st = lan9645_reg_rmw32_desc(
    ctx->reg,
    &LAN9645_REG_DEV_MAC_MODE_CFG,
    0u,
    (uint32_t)port,
    0u,
    mode_mask,
    mode_value
  );
  if (st != APP_OK) {
    lan9645_core_report_port_cfg_fail(
      port,
      lan9645_reg_desc_rel_addr(&LAN9645_REG_DEV_MAC_MODE_CFG, (uint32_t)port, 0u),
      st,
      LAN9645_CORE_PORT_STEP_MODE_MAC_SET
    );
    return st;
  }

  st = lan9645_core_verify_masked_desc(
    ctx,
    &LAN9645_REG_DEV_MAC_MODE_CFG,
    (uint32_t)port,
    0u,
    mode_mask,
    mode_value,
    port,
    LAN9645_CORE_PORT_STEP_MODE_MAC_VERIFY
  );
  if (st != APP_OK) {
    return st;
  }

  ena_mask = LAN9645_DEV_MAC_ENA_CFG_TX_ENA | LAN9645_DEV_MAC_ENA_CFG_RX_ENA;
  ena_value = ena_mask;
  st = lan9645_reg_rmw32_desc(
    ctx->reg,
    &LAN9645_REG_DEV_MAC_ENA_CFG,
    0u,
    (uint32_t)port,
    0u,
    ena_mask,
    ena_value
  );
  if (st != APP_OK) {
    lan9645_core_report_port_cfg_fail(
      port,
      lan9645_reg_desc_rel_addr(&LAN9645_REG_DEV_MAC_ENA_CFG, (uint32_t)port, 0u),
      st,
      LAN9645_CORE_PORT_STEP_MODE_ENA_SET
    );
    return st;
  }

  return lan9645_core_verify_masked_desc(
    ctx,
    &LAN9645_REG_DEV_MAC_ENA_CFG,
    (uint32_t)port,
    0u,
    ena_mask,
    ena_value,
    port,
    LAN9645_CORE_PORT_STEP_MODE_ENA_VERIFY
  );
}

/**
 * @brief Reads one switch-port administrative and MAC operating snapshot.
 *
 * @param[in,out] ctx Core context.
 * @param[in] port Switch port index.
 * @param[out] out Destination configuration snapshot.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_core_get_port_cfg(lan9645_core_t *ctx, uint8_t port, lan9645_core_port_cfg_t *out)
{
  uint32_t qsys = 0u;
  uint32_t clock_cfg = 0u;
  uint32_t mac_mode = 0u;
  uint32_t mac_ena = 0u;
  uint32_t speed_code = 0u;
  app_status_t st = lan9645_core_check_ctx(ctx);
  if (st != APP_OK) {
    return st;
  }
  if (out == NULL) {
    return APP_EINVAL;
  }
  if (port > LAN9645_CORE_MAX_DEV_PORT) {
    return APP_EINVAL;
  }

  st = lan9645_reg_read32_desc(ctx->reg, &LAN9645_REG_QSYS_SW_PORT_MODE, 0u, 0u, (uint32_t)port, &qsys);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_reg_read32_desc(ctx->reg, &LAN9645_REG_DEV_CLOCK_CFG, 0u, (uint32_t)port, 0u, &clock_cfg);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_reg_read32_desc(ctx->reg, &LAN9645_REG_DEV_MAC_MODE_CFG, 0u, (uint32_t)port, 0u, &mac_mode);
  if (st != APP_OK) {
    return st;
  }
  st = lan9645_reg_read32_desc(ctx->reg, &LAN9645_REG_DEV_MAC_ENA_CFG, 0u, (uint32_t)port, 0u, &mac_ena);
  if (st != APP_OK) {
    return st;
  }

  speed_code = LAN9645_FIELD_GET(LAN9645_DEV_CLOCK_CFG_LINK_SPEED, clock_cfg);
  memset(out, 0, sizeof(*out));
  out->admin_up = ((qsys & LAN9645_QSYS_SW_PORT_MODE_PORT_ENA) != 0u) ? 1u : 0u;
  out->speed_mbps = lan9645_core_speed_from_code(speed_code);
  out->full_duplex = ((mac_mode & LAN9645_DEV_MAC_MODE_CFG_FDX_ENA) != 0u) ? 1u : 0u;
  out->mac_rx_ena = ((mac_ena & LAN9645_DEV_MAC_ENA_CFG_RX_ENA) != 0u) ? 1u : 0u;
  out->mac_tx_ena = ((mac_ena & LAN9645_DEV_MAC_ENA_CFG_TX_ENA) != 0u) ? 1u : 0u;
  return APP_OK;
}

/**
 * @brief Backward-compatible wrapper for setting QSYS port enable.
 *
 * @param[in,out] ctx Core context.
 * @param[in] port Switch port index.
 * @param[in] enable Set to 1 to enable the port.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_core_set_port_enable(lan9645_core_t *ctx, uint8_t port, uint8_t enable)
{
  return lan9645_core_set_port_admin(ctx, port, enable);
}

/**
 * @brief Samples RGMII in-band status bits for the selected interface.
 *
 * @param[in,out] ctx Core context.
 * @param[in] rgmii_idx RGMII interface index.
 * @param[out] link_up Optional link status output.
 * @param[out] duplex Optional duplex output.
 * @param[out] speed_code Optional speed code output.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_core_get_rgmii_link(
  lan9645_core_t *ctx,
  uint8_t rgmii_idx,
  uint8_t *link_up,
  uint8_t *duplex,
  uint8_t *speed_code
)
{
  uint32_t val = 0u;
  app_status_t st = lan9645_core_check_ctx(ctx);
  if (st != APP_OK) {
    return st;
  }
  if (rgmii_idx > 1u) {
    return APP_EINVAL;
  }

  st = lan9645_reg_read32_desc(
    ctx->reg,
    &LAN9645_REG_HSIO_RGMII_CFG,
    0u,
    0u,
    (uint32_t)rgmii_idx,
    &val
  );
  if (st != APP_OK) {
    return st;
  }

  if (link_up != NULL) {
    *link_up = ((val & LAN9645_HSIO_RGMII_CFG_IB_RX_LINK_STATUS) != 0u) ? 1u : 0u;
  }
  if (duplex != NULL) {
    *duplex = ((val & LAN9645_HSIO_RGMII_CFG_IB_RX_DUPLEX) != 0u) ? 1u : 0u;
  }
  if (speed_code != NULL) {
    *speed_code = (uint8_t)LAN9645_FIELD_GET(LAN9645_HSIO_RGMII_CFG_IB_RX_SPEED, val);
  }

  return APP_OK;
}

/**
 * @brief Captures the main interrupt-related register set used by the CLI.
 *
 * @param[in,out] ctx Core context.
 * @param[out] out Destination IRQ snapshot.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_core_get_irq_regs(lan9645_core_t *ctx, lan9645_core_irq_regs_t *out)
{
  app_status_t st = lan9645_core_check_ctx(ctx);
  if (st != APP_OK) {
    return st;
  }
  if (out == NULL) {
    return APP_EINVAL;
  }
  if (ctx->spi_if_ready == 0u) {
    return APP_ESTATE;
  }

  memset(out, 0, sizeof(*out));

  st = lan9645_reg_read32_desc(
    ctx->reg,
    &LAN9645_REG_ANA_ANAINTR,
    0u,
    0u,
    0u,
    &out->ana_anaintr
  );
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_reg_read32_desc(
    ctx->reg,
    &LAN9645_REG_ANA_ANEVENTS,
    0u,
    0u,
    0u,
    &out->ana_anevents
  );
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_reg_read32_desc(
    ctx->reg,
    &LAN9645_REG_CPU_INTR_ENA,
    0u,
    0u,
    0u,
    &out->intr_ena
  );
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_reg_read32_desc(
    ctx->reg,
    &LAN9645_REG_CPU_INTR_IDENT,
    0u,
    0u,
    0u,
    &out->intr_ident
  );
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_reg_read32_desc(
    ctx->reg,
    &LAN9645_REG_CPU_INTR_IDENT1,
    0u,
    0u,
    0u,
    &out->intr_ident1
  );
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_reg_read32_desc(
    ctx->reg,
    &LAN9645_REG_CPU_DEV_INTR_ENA,
    0u,
    0u,
    0u,
    &out->dev_intr_ena
  );
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_reg_read32_desc(
    ctx->reg,
    &LAN9645_REG_CPU_DEV_INTR_IDENT,
    0u,
    0u,
    0u,
    &out->dev_intr_ident
  );
  if (st != APP_OK) {
    return st;
  }

  return APP_OK;
}

/**
 * @brief Acknowledges ANA interrupt events using write-one-to-clear semantics.
 *
 * @param[in,out] ctx Core context.
 * @param[in] ana_anevents_mask Events to acknowledge.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_core_ack_irq(lan9645_core_t *ctx, uint32_t ana_anevents_mask)
{
  app_status_t st = lan9645_core_check_ctx(ctx);
  if (st != APP_OK) {
    return st;
  }
  if (ctx->spi_if_ready == 0u) {
    return APP_ESTATE;
  }
  if (ana_anevents_mask == 0u) {
    return APP_OK;
  }

  /* ANEVENTS is treated as W1C in this service path. */
  return lan9645_reg_write32_desc(
    ctx->reg,
    &LAN9645_REG_ANA_ANEVENTS,
    0u,
    0u,
    0u,
    ana_anevents_mask
  );
}

/**
 * @brief Acknowledges ANA events and readback-verifies that selected bits cleared.
 *
 * @param[in,out] ctx Core context.
 * @param[in] ana_anevents_mask ANA events to clear with W1C semantics.
 * @param[out] remaining_mask Optional mask of still-set events after acknowledge.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_core_ack_irq_checked(
  lan9645_core_t *ctx,
  uint32_t ana_anevents_mask,
  uint32_t *remaining_mask
)
{
  uint32_t readback = 0u;
  uint32_t remain = 0u;
  app_status_t st = APP_OK;

  if (remaining_mask != NULL) {
    *remaining_mask = 0u;
  }

  st = lan9645_core_ack_irq(ctx, ana_anevents_mask);
  if (st != APP_OK) {
    return st;
  }
  if (ana_anevents_mask == 0u) {
    return APP_OK;
  }

  st = lan9645_reg_read32_desc(
    ctx->reg,
    &LAN9645_REG_ANA_ANEVENTS,
    0u,
    0u,
    0u,
    &readback
  );
  if (st != APP_OK) {
    return st;
  }

  remain = readback & ana_anevents_mask;
  if (remaining_mask != NULL) {
    *remaining_mask = remain;
  }
  if (remain != 0u) {
    return APP_EIO;
  }

  return APP_OK;
}

/**
 * @brief Converts IRQ register snapshots into deterministic action recommendations.
 *
 * @param[in] regs IRQ register snapshot.
 * @param[out] out Decoded analysis output.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_core_eval_irq_actions(
  const lan9645_core_irq_regs_t *regs,
  lan9645_core_irq_analysis_t *out
)
{
  if ((regs == NULL) || (out == NULL)) {
    return APP_EINVAL;
  }

  memset(out, 0, sizeof(*out));
  out->ana_anevents = regs->ana_anevents;
  out->intr_ident = regs->intr_ident;
  out->intr_ident1 = regs->intr_ident1;
  out->dev_pending = LAN9645_FIELD_GET(LAN9645_CPU_DEV_INTR_MASK, regs->dev_intr_ident);
  out->drop_events = regs->ana_anevents & LAN9645_CORE_ANA_EVT_DROP_MASK;
  out->warn_events = regs->ana_anevents & LAN9645_CORE_ANA_EVT_WARN_MASK;
  out->err_events = regs->ana_anevents & LAN9645_CORE_ANA_EVT_ERR_MASK;

  if ((regs->ana_anevents != 0u) ||
      (regs->intr_ident != 0u) ||
      (regs->intr_ident1 != 0u) ||
      (out->dev_pending != 0u)) {
    out->has_cause = 1u;
    out->action_mask |= LAN9645_CORE_IRQ_ACTION_REPORT_CAUSE;
  }

  if (regs->ana_anevents != 0u) {
    out->ack_required = 1u;
    out->action_mask |= LAN9645_CORE_IRQ_ACTION_ACK_ANA_EVENTS;
  }

  if (out->drop_events != 0u) {
    out->action_mask |= LAN9645_CORE_IRQ_ACTION_REPORT_DROP_ALERT;
  }

  if (out->err_events != 0u) {
    out->severity_code = LAN9645_CORE_IRQ_SEV_ERR;
    out->action_mask |= LAN9645_CORE_IRQ_ACTION_ESCALATE_ERROR_LED;
  } else if ((out->warn_events != 0u) ||
             (regs->intr_ident != 0u) ||
             (regs->intr_ident1 != 0u) ||
             (out->dev_pending != 0u)) {
    out->severity_code = LAN9645_CORE_IRQ_SEV_WARN;
  } else {
    out->severity_code = LAN9645_CORE_IRQ_SEV_INFO;
  }

  return APP_OK;
}
