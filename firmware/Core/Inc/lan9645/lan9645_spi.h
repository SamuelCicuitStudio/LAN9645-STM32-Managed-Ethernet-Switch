#ifndef LAN9645_SPI_H
#define LAN9645_SPI_H

/**
 * @file lan9645_spi.h
 * @brief Raw SPI transport layer for LAN9645 management register access.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "app_common_types.h"
#include "main.h"

/** @brief Mask for the 25-bit SPI address field used by the LAN9645 transport. */
#define LAN9645_SPI_REG_ADDR_MASK (0x01FFFFFFu)
/** @brief SPI command flag placed in the first address byte for write transactions. */
#define LAN9645_SPI_WRITE_FLAG    (0x80u)

/** @brief Default per-transaction timeout used by the SPI helper. */
#define LAN9645_SPI_DEFAULT_TIMEOUT_MS (20u)
/** @brief Default retry count used after a failed SPI access. */
#define LAN9645_SPI_DEFAULT_RETRIES    (2u)
/** @brief Default read padding used for the current LAN9645 SPI integration. */
#define LAN9645_SPI_DEFAULT_PADDING    (15u)
/** @brief Maximum read padding supported by this driver. */
#define LAN9645_SPI_MAX_PADDING        (15u)
/** @brief Estimated internal LAN9645 register access latency in microseconds. */
#define LAN9645_SPI_REG_ACCESS_TIME_US (2u)
/** @brief Numerator used by the read-padding estimation formula. */
#define LAN9645_SPI_PADDING_NUM_CONST  (1500u)
/** @brief Denominator used by the read-padding estimation formula. */
#define LAN9645_SPI_PADDING_DEN_CONST  (8000u)

/**
 * @brief SPI bus context used for all LAN9645 register transactions.
 */
typedef struct {
  SPI_HandleTypeDef *hspi; /**< STM32 HAL SPI instance bound to the LAN9645. */
  GPIO_TypeDef *cs_port;   /**< GPIO port connected to the LAN9645 chip select. */
  uint16_t cs_pin;         /**< GPIO pin connected to the LAN9645 chip select. */
  uint8_t padding_bytes;   /**< Number of dummy bytes inserted before SPI read data. */
  uint8_t retries;         /**< Number of retries attempted after a failed transaction. */
  uint32_t timeout_ms;     /**< HAL timeout used for each blocking SPI transfer. */
  uint8_t initialized;     /**< Set after the SPI bus context has been initialized. */
} lan9645_spi_t;

/**
 * @brief Initializes the LAN9645 SPI transport helper.
 *
 * @param[out] bus SPI bus context to initialize.
 * @param[in] hspi STM32 HAL SPI handle.
 * @param[in] cs_port GPIO port connected to chip select.
 * @param[in] cs_pin GPIO pin connected to chip select.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_spi_init(
  lan9645_spi_t *bus,
  SPI_HandleTypeDef *hspi,
  GPIO_TypeDef *cs_port,
  uint16_t cs_pin
);

/**
 * @brief Overrides the number of dummy bytes inserted before read data.
 *
 * @param[in,out] bus SPI bus context.
 * @param[in] padding_bytes Read padding byte count.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_spi_set_padding(lan9645_spi_t *bus, uint8_t padding_bytes);

/**
 * @brief Overrides the per-transfer timeout used by the SPI helper.
 *
 * @param[in,out] bus SPI bus context.
 * @param[in] timeout_ms Timeout in milliseconds.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_spi_set_timeout(lan9645_spi_t *bus, uint32_t timeout_ms);

/**
 * @brief Overrides the retry count used after failed SPI accesses.
 *
 * @param[in,out] bus SPI bus context.
 * @param[in] retries Number of retry attempts.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_spi_set_retries(lan9645_spi_t *bus, uint8_t retries);

/**
 * @brief Estimates the required LAN9645 SPI read padding from SPI clock rate.
 *
 * @param[in] spi_clock_hz Active SPI clock in hertz.
 * @param[out] padding_bytes Destination for the calculated padding count.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_spi_calc_padding(uint32_t spi_clock_hz, uint8_t *padding_bytes);

/**
 * @brief Reads one 32-bit LAN9645 register using the raw SPI transport.
 *
 * @param[in,out] bus SPI bus context.
 * @param[in] reg_addr Absolute register word address.
 * @param[out] value Destination for the register value.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_spi_read32(lan9645_spi_t *bus, uint32_t reg_addr, uint32_t *value);

/**
 * @brief Writes one 32-bit LAN9645 register using the raw SPI transport.
 *
 * @param[in,out] bus SPI bus context.
 * @param[in] reg_addr Absolute register word address.
 * @param[in] value Register value to write.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_spi_write32(lan9645_spi_t *bus, uint32_t reg_addr, uint32_t value);

#ifdef __cplusplus
}
#endif

#endif /* LAN9645_SPI_H */
