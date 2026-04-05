#ifndef LAN9645_SPI_H
#define LAN9645_SPI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "app_common_types.h"
#include "main.h"

#define LAN9645_SPI_REG_ADDR_MASK (0x01FFFFFFu)
#define LAN9645_SPI_WRITE_FLAG    (0x80u)

#define LAN9645_SPI_DEFAULT_TIMEOUT_MS (20u)
#define LAN9645_SPI_DEFAULT_RETRIES    (2u)
#define LAN9645_SPI_DEFAULT_PADDING    (1u)
#define LAN9645_SPI_MAX_PADDING        (8u)

typedef struct {
  SPI_HandleTypeDef *hspi;
  GPIO_TypeDef *cs_port;
  uint16_t cs_pin;
  uint8_t padding_bytes;
  uint8_t retries;
  uint32_t timeout_ms;
  uint8_t initialized;
} lan9645_spi_t;

app_status_t lan9645_spi_init(
  lan9645_spi_t *bus,
  SPI_HandleTypeDef *hspi,
  GPIO_TypeDef *cs_port,
  uint16_t cs_pin
);

app_status_t lan9645_spi_set_padding(lan9645_spi_t *bus, uint8_t padding_bytes);
app_status_t lan9645_spi_set_timeout(lan9645_spi_t *bus, uint32_t timeout_ms);
app_status_t lan9645_spi_set_retries(lan9645_spi_t *bus, uint8_t retries);

app_status_t lan9645_spi_read32(lan9645_spi_t *bus, uint32_t reg_addr, uint32_t *value);
app_status_t lan9645_spi_write32(lan9645_spi_t *bus, uint32_t reg_addr, uint32_t value);

#ifdef __cplusplus
}
#endif

#endif /* LAN9645_SPI_H */
