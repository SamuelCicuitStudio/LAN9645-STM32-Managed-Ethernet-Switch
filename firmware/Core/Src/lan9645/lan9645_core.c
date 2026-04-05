#include "lan9645/lan9645_core.h"

/**
 * @file lan9645_core.c
 * @brief High-level LAN9645 core helpers for reset, SPI setup, and IRQ snapshots.
 */

#include <stddef.h>
#include <string.h>

#include "lan9645/lan9645_spi.h"

#define LAN9645_SPI_BYTE_ORDER_LE      (0x00000000u)

#define LAN9645_CORE_SPI_SETUP_DELAY_MS (5u)

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
 * @brief Enables or disables one switch port.
 *
 * @param[in,out] ctx Core context.
 * @param[in] port Switch port index.
 * @param[in] enable Set to 1 to enable the port.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_core_set_port_enable(lan9645_core_t *ctx, uint8_t port, uint8_t enable)
{
  uint32_t value = (enable != 0u) ? LAN9645_QSYS_SW_PORT_MODE_PORT_ENA : 0u;
  app_status_t st = lan9645_core_check_ctx(ctx);
  if (st != APP_OK) {
    return st;
  }
  if (port > 9u) {
    return APP_EINVAL;
  }

  return lan9645_reg_rmw32_desc(
    ctx->reg,
    &LAN9645_REG_QSYS_SW_PORT_MODE,
    0u,
    0u,
    (uint32_t)port,
    LAN9645_QSYS_SW_PORT_MODE_PORT_ENA,
    value
  );
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
