#include "lan9645/lan9645_spi.h"

/**
 * @file lan9645_spi.c
 * @brief Raw SPI framing for LAN9645 read and write transactions.
 */

#include <stddef.h>
#include <string.h>

#define LAN9645_SPI_REG_ADDR_SIZE   (3u)
#define LAN9645_SPI_REG_VALUE_SIZE  (4u)
#define LAN9645_SPI_READ_FRAME_MAX  (LAN9645_SPI_REG_ADDR_SIZE + LAN9645_SPI_MAX_PADDING + LAN9645_SPI_REG_VALUE_SIZE)

/**
 * @brief Converts a byte-addressed register location into the 23-bit SPI word address.
 *
 * @param[in] reg_addr Absolute register address in bytes.
 *
 * @return SPI address payload used on the wire.
 */
static uint32_t lan9645_spi_reg_to_si_addr(uint32_t reg_addr)
{
  return (reg_addr & LAN9645_SPI_REG_ADDR_MASK) >> 2;
}

/**
 * @brief Drives chip select active.
 *
 * @param[in] bus SPI bus context.
 */
static void lan9645_spi_cs_assert(const lan9645_spi_t *bus)
{
  HAL_GPIO_WritePin(bus->cs_port, bus->cs_pin, GPIO_PIN_RESET);
}

/**
 * @brief Releases chip select after a transaction.
 *
 * @param[in] bus SPI bus context.
 */
static void lan9645_spi_cs_deassert(const lan9645_spi_t *bus)
{
  HAL_GPIO_WritePin(bus->cs_port, bus->cs_pin, GPIO_PIN_SET);
}

/**
 * @brief Sends a transmit-only SPI frame.
 *
 * @param[in,out] bus SPI bus context.
 * @param[in] tx Transmit buffer.
 * @param[in] len Number of bytes to send.
 *
 * @return @ref APP_OK on success.
 */
static app_status_t lan9645_spi_tx(lan9645_spi_t *bus, const uint8_t *tx, uint16_t len)
{
  if (HAL_SPI_Transmit(bus->hspi, (uint8_t *)tx, len, bus->timeout_ms) != HAL_OK) {
    return APP_EIO;
  }

  return APP_OK;
}

/**
 * @brief Sends and receives one SPI frame.
 *
 * @param[in,out] bus SPI bus context.
 * @param[in] tx Transmit buffer.
 * @param[out] rx Receive buffer.
 * @param[in] len Number of bytes to exchange.
 *
 * @return @ref APP_OK on success.
 */
static app_status_t lan9645_spi_txrx(
  lan9645_spi_t *bus,
  const uint8_t *tx,
  uint8_t *rx,
  uint16_t len
)
{
  if (HAL_SPI_TransmitReceive(bus->hspi, (uint8_t *)tx, rx, len, bus->timeout_ms) != HAL_OK) {
    return APP_EIO;
  }

  return APP_OK;
}

/**
 * @brief Validates that the SPI context is ready for transfers.
 *
 * @param[in] bus SPI bus context to validate.
 *
 * @return @ref APP_OK when the context is usable.
 */
static app_status_t lan9645_spi_check_ctx(const lan9645_spi_t *bus)
{
  if (bus == NULL) {
    return APP_EINVAL;
  }
  if ((bus->initialized == 0u) || (bus->hspi == NULL) || (bus->cs_port == NULL)) {
    return APP_ESTATE;
  }
  if (bus->padding_bytes > LAN9645_SPI_MAX_PADDING) {
    return APP_ESTATE;
  }

  return APP_OK;
}

/**
 * @brief Initializes the SPI transport context.
 *
 * @param[out] bus SPI bus context to initialize.
 * @param[in] hspi STM32 HAL SPI handle.
 * @param[in] cs_port GPIO port driving chip select.
 * @param[in] cs_pin GPIO pin driving chip select.
 *
 * @return @ref APP_OK on success.
 */
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

/**
 * @brief Overrides the read-padding byte count.
 *
 * @param[in,out] bus SPI bus context.
 * @param[in] padding_bytes Padding byte count to install.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_spi_set_padding(lan9645_spi_t *bus, uint8_t padding_bytes)
{
  app_status_t st = lan9645_spi_check_ctx(bus);
  if (st != APP_OK) {
    return st;
  }
  if (padding_bytes > LAN9645_SPI_MAX_PADDING) {
    return APP_EINVAL;
  }

  bus->padding_bytes = padding_bytes;
  return APP_OK;
}

/**
 * @brief Overrides the blocking transfer timeout used by HAL SPI calls.
 *
 * @param[in,out] bus SPI bus context.
 * @param[in] timeout_ms Timeout in milliseconds.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_spi_set_timeout(lan9645_spi_t *bus, uint32_t timeout_ms)
{
  app_status_t st = lan9645_spi_check_ctx(bus);
  if (st != APP_OK) {
    return st;
  }
  if (timeout_ms == 0u) {
    return APP_EINVAL;
  }

  bus->timeout_ms = timeout_ms;
  return APP_OK;
}

/**
 * @brief Overrides the retry count used for failed transactions.
 *
 * @param[in,out] bus SPI bus context.
 * @param[in] retries Number of retries after the first attempt.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_spi_set_retries(lan9645_spi_t *bus, uint8_t retries)
{
  app_status_t st = lan9645_spi_check_ctx(bus);
  if (st != APP_OK) {
    return st;
  }

  bus->retries = retries;
  return APP_OK;
}

/**
 * @brief Estimates the required number of read padding bytes.
 *
 * @param[in] spi_clock_hz SPI clock in hertz.
 * @param[out] padding_bytes Destination for the calculated padding count.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_spi_calc_padding(uint32_t spi_clock_hz, uint8_t *padding_bytes)
{
  uint64_t khz = 0u;
  uint64_t num = 0u;
  uint64_t pad = 0u;

  if (padding_bytes == NULL) {
    return APP_EINVAL;
  }

  khz = (uint64_t)(spi_clock_hz / 1000u);
  num = (khz * (uint64_t)LAN9645_SPI_REG_ACCESS_TIME_US) + (uint64_t)LAN9645_SPI_PADDING_NUM_CONST;
  pad = (num + ((uint64_t)LAN9645_SPI_PADDING_DEN_CONST - 1u)) / (uint64_t)LAN9645_SPI_PADDING_DEN_CONST;

  if (pad > (uint64_t)LAN9645_SPI_MAX_PADDING) {
    return APP_EINVAL;
  }

  *padding_bytes = (uint8_t)pad;
  return APP_OK;
}

/**
 * @brief Reads one 32-bit register over the LAN9645 SPI management interface.
 *
 * @param[in,out] bus SPI bus context.
 * @param[in] reg_addr Absolute register address.
 * @param[out] value Destination register value.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_spi_read32(lan9645_spi_t *bus, uint32_t reg_addr, uint32_t *value)
{
  uint8_t tx[LAN9645_SPI_READ_FRAME_MAX];
  uint8_t rx[LAN9645_SPI_READ_FRAME_MAX];
  uint32_t si_addr = 0u;
  uint16_t frame_len = 0u;
  uint16_t value_offset = 0u;
  app_status_t st = APP_EIO;
  uint8_t attempt = 0u;

  st = lan9645_spi_check_ctx(bus);
  if (st != APP_OK) {
    return st;
  }
  if (value == NULL) {
    return APP_EINVAL;
  }
  if ((reg_addr & 0x3u) != 0u) {
    return APP_EINVAL;
  }

  si_addr = lan9645_spi_reg_to_si_addr(reg_addr);
  frame_len = (uint16_t)(LAN9645_SPI_REG_ADDR_SIZE + bus->padding_bytes + LAN9645_SPI_REG_VALUE_SIZE);
  value_offset = (uint16_t)(LAN9645_SPI_REG_ADDR_SIZE + bus->padding_bytes);

  memset(tx, 0, sizeof(tx));
  tx[0] = (uint8_t)((si_addr >> 16) & 0x7Fu);
  tx[1] = (uint8_t)((si_addr >> 8) & 0xFFu);
  tx[2] = (uint8_t)(si_addr & 0xFFu);

  for (attempt = 0u; attempt <= bus->retries; ++attempt) {
    lan9645_spi_cs_assert(bus);
    st = lan9645_spi_txrx(bus, tx, rx, frame_len);
    lan9645_spi_cs_deassert(bus);

    if (st == APP_OK) {
      *value =
        ((uint32_t)rx[value_offset + 0u]) |
        ((uint32_t)rx[value_offset + 1u] << 8) |
        ((uint32_t)rx[value_offset + 2u] << 16) |
        ((uint32_t)rx[value_offset + 3u] << 24);
      return APP_OK;
    }

    HAL_Delay(1u);
  }

  return st;
}

/**
 * @brief Writes one 32-bit register over the LAN9645 SPI management interface.
 *
 * @param[in,out] bus SPI bus context.
 * @param[in] reg_addr Absolute register address.
 * @param[in] value Register value to write.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_spi_write32(lan9645_spi_t *bus, uint32_t reg_addr, uint32_t value)
{
  uint32_t si_addr = 0u;
  uint8_t tx[7];
  app_status_t st = APP_EIO;
  uint8_t attempt = 0u;

  st = lan9645_spi_check_ctx(bus);
  if (st != APP_OK) {
    return st;
  }
  if ((reg_addr & 0x3u) != 0u) {
    return APP_EINVAL;
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
