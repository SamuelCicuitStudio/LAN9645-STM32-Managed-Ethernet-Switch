#include "lan9645/lan9645_spi.h"

#include <stddef.h>
#include <string.h>

static uint32_t lan9645_spi_reg_to_si_addr(uint32_t reg_addr)
{
  return (reg_addr & LAN9645_SPI_REG_ADDR_MASK) >> 2;
}

static void lan9645_spi_cs_assert(const lan9645_spi_t *bus)
{
  HAL_GPIO_WritePin(bus->cs_port, bus->cs_pin, GPIO_PIN_RESET);
}

static void lan9645_spi_cs_deassert(const lan9645_spi_t *bus)
{
  HAL_GPIO_WritePin(bus->cs_port, bus->cs_pin, GPIO_PIN_SET);
}

static app_status_t lan9645_spi_tx(lan9645_spi_t *bus, const uint8_t *tx, uint16_t len)
{
  if (HAL_SPI_Transmit(bus->hspi, (uint8_t *)tx, len, bus->timeout_ms) != HAL_OK) {
    return APP_EIO;
  }

  return APP_OK;
}

static app_status_t lan9645_spi_rx(lan9645_spi_t *bus, uint8_t *rx, uint16_t len)
{
  if (HAL_SPI_Receive(bus->hspi, rx, len, bus->timeout_ms) != HAL_OK) {
    return APP_EIO;
  }

  return APP_OK;
}

app_status_t lan9645_spi_init(
  lan9645_spi_t *bus,
  SPI_HandleTypeDef *hspi,
  GPIO_TypeDef *cs_port,
  uint16_t cs_pin
)
{
  if ((bus == NULL) || (hspi == NULL) || (cs_port == NULL) || (cs_pin == 0u)) {
    return APP_EINVAL;
  }

  if (hspi->Init.DataSize != SPI_DATASIZE_8BIT) {
    return APP_ESTATE;
  }

  memset(bus, 0, sizeof(*bus));
  bus->hspi = hspi;
  bus->cs_port = cs_port;
  bus->cs_pin = cs_pin;
  bus->padding_bytes = LAN9645_SPI_DEFAULT_PADDING;
  bus->retries = LAN9645_SPI_DEFAULT_RETRIES;
  bus->timeout_ms = LAN9645_SPI_DEFAULT_TIMEOUT_MS;
  bus->initialized = 1u;

  /* Keep CS inactive unless an SPI transaction is in progress. */
  lan9645_spi_cs_deassert(bus);

  return APP_OK;
}

app_status_t lan9645_spi_set_padding(lan9645_spi_t *bus, uint8_t padding_bytes)
{
  if (bus == NULL) {
    return APP_EINVAL;
  }
  if (padding_bytes > LAN9645_SPI_MAX_PADDING) {
    return APP_EINVAL;
  }

  bus->padding_bytes = padding_bytes;
  return APP_OK;
}

app_status_t lan9645_spi_set_timeout(lan9645_spi_t *bus, uint32_t timeout_ms)
{
  if (bus == NULL) {
    return APP_EINVAL;
  }
  if (timeout_ms == 0u) {
    return APP_EINVAL;
  }

  bus->timeout_ms = timeout_ms;
  return APP_OK;
}

app_status_t lan9645_spi_set_retries(lan9645_spi_t *bus, uint8_t retries)
{
  if (bus == NULL) {
    return APP_EINVAL;
  }

  bus->retries = retries;
  return APP_OK;
}

app_status_t lan9645_spi_read32(lan9645_spi_t *bus, uint32_t reg_addr, uint32_t *value)
{
  uint32_t si_addr = 0u;
  uint8_t cmd[3];
  uint8_t pad[LAN9645_SPI_MAX_PADDING] = {0};
  uint8_t rx[4];
  app_status_t st = APP_EIO;
  uint8_t attempt = 0u;

  if ((bus == NULL) || (value == NULL)) {
    return APP_EINVAL;
  }
  if (bus->initialized == 0u) {
    return APP_ESTATE;
  }

  si_addr = lan9645_spi_reg_to_si_addr(reg_addr);

  cmd[0] = (uint8_t)((si_addr >> 16) & 0x7Fu);
  cmd[1] = (uint8_t)((si_addr >> 8) & 0xFFu);
  cmd[2] = (uint8_t)(si_addr & 0xFFu);

  for (attempt = 0u; attempt <= bus->retries; ++attempt) {
    lan9645_spi_cs_assert(bus);

    st = lan9645_spi_tx(bus, cmd, (uint16_t)sizeof(cmd));
    if ((st == APP_OK) && (bus->padding_bytes > 0u)) {
      st = lan9645_spi_tx(bus, pad, bus->padding_bytes);
    }
    if (st == APP_OK) {
      st = lan9645_spi_rx(bus, rx, (uint16_t)sizeof(rx));
    }

    lan9645_spi_cs_deassert(bus);

    if (st == APP_OK) {
      *value =
        ((uint32_t)rx[0]) |
        ((uint32_t)rx[1] << 8) |
        ((uint32_t)rx[2] << 16) |
        ((uint32_t)rx[3] << 24);
      return APP_OK;
    }

    HAL_Delay(1u);
  }

  return st;
}

app_status_t lan9645_spi_write32(lan9645_spi_t *bus, uint32_t reg_addr, uint32_t value)
{
  uint32_t si_addr = 0u;
  uint8_t tx[7];
  app_status_t st = APP_EIO;
  uint8_t attempt = 0u;

  if (bus == NULL) {
    return APP_EINVAL;
  }
  if (bus->initialized == 0u) {
    return APP_ESTATE;
  }

  si_addr = lan9645_spi_reg_to_si_addr(reg_addr);

  tx[0] = (uint8_t)(LAN9645_SPI_WRITE_FLAG | ((si_addr >> 16) & 0x7Fu));
  tx[1] = (uint8_t)((si_addr >> 8) & 0xFFu);
  tx[2] = (uint8_t)(si_addr & 0xFFu);
  tx[3] = (uint8_t)(value & 0xFFu);
  tx[4] = (uint8_t)((value >> 8) & 0xFFu);
  tx[5] = (uint8_t)((value >> 16) & 0xFFu);
  tx[6] = (uint8_t)((value >> 24) & 0xFFu);

  for (attempt = 0u; attempt <= bus->retries; ++attempt) {
    lan9645_spi_cs_assert(bus);
    st = lan9645_spi_tx(bus, tx, (uint16_t)sizeof(tx));
    lan9645_spi_cs_deassert(bus);

    if (st == APP_OK) {
      return APP_OK;
    }

    HAL_Delay(1u);
  }

  return st;
}
