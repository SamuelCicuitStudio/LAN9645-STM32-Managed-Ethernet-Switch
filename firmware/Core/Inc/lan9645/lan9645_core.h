#ifndef LAN9645_CORE_H
#define LAN9645_CORE_H

/**
 * @file lan9645_core.h
 * @brief High-level LAN9645 core control helpers used during switch bring-up.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "app_common_types.h"
#include "lan9645/lan9645_reg.h"
#include "main.h"

/** @brief Default timeout used while waiting for the LAN9645 soft reset to complete. */
#define LAN9645_CORE_SOFT_RESET_TIMEOUT_MS (200u)

/**
 * @brief Runtime context for the LAN9645 switch core.
 */
typedef struct {
  lan9645_reg_t *reg;      /**< Register access layer bound to the switch SPI transport. */
  GPIO_TypeDef *rst_port;  /**< MCU GPIO port connected to LAN_RST. */
  uint16_t rst_pin;        /**< MCU GPIO pin connected to LAN_RST. */
  uint32_t spi_clock_hz;   /**< Last SPI clock used for interface setup and padding selection. */
  uint8_t spi_if_ready;    /**< Set when IF_CFGSTAT confirms the SPI management interface is ready. */
  uint8_t initialized;     /**< Set after the core context has been initialized. */
} lan9645_core_t;

/**
 * @brief Snapshot of the interrupt-related LAN9645 registers used in bring-up.
 */
typedef struct {
  uint32_t ana_anaintr;    /**< ANA interrupt status register. */
  uint32_t ana_anevents;   /**< ANA event latch register. */
  uint32_t intr_ena;       /**< CPU interrupt enable register. */
  uint32_t intr_ident;     /**< CPU interrupt identify register 0. */
  uint32_t intr_ident1;    /**< CPU interrupt identify register 1. */
  uint32_t dev_intr_ena;   /**< Device interrupt enable snapshot for the selected port. */
  uint32_t dev_intr_ident; /**< Device interrupt identify snapshot for the selected port. */
} lan9645_core_irq_regs_t;

/**
 * @brief Initializes the LAN9645 core helper.
 *
 * @param[out] ctx Core context to initialize.
 * @param[in] reg Register access layer already bound to the SPI bus.
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
);

/**
 * @brief Pulses the external LAN9645 reset line.
 *
 * @param[in,out] ctx Core context.
 * @param[in] assert_ms Time to keep reset asserted.
 * @param[in] release_ms Time to wait after releasing reset.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_core_hw_reset(lan9645_core_t *ctx, uint32_t assert_ms, uint32_t release_ms);

/**
 * @brief Programs the SPI management interface mode and padding policy.
 *
 * @param[in,out] ctx Core context.
 * @param[in] spi_clock_hz Active SPI clock used by the STM32.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_core_spi_setup(lan9645_core_t *ctx, uint32_t spi_clock_hz);

/**
 * @brief Reads the current SPI management interface status from the switch.
 *
 * @param[in,out] ctx Core context.
 * @param[out] if_ctrl Optional copy of the IF_CTRL register.
 * @param[out] if_cfgstat Optional copy of the IF_CFGSTAT register.
 * @param[out] padding_bytes Optional calculated padding byte count used by the STM32 driver.
 * @param[out] spi_clock_hz Optional copy of the configured SPI frequency.
 * @param[out] spi_if_ready Optional flag that reflects interface readiness.
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
);

/**
 * @brief Reads the LAN9645 chip identifier register.
 *
 * @param[in,out] ctx Core context.
 * @param[out] chip_id Destination for the raw chip ID register value.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_core_read_chip_id(lan9645_core_t *ctx, uint32_t *chip_id);

/**
 * @brief Requests a LAN9645 soft reset and waits for completion.
 *
 * @param[in,out] ctx Core context.
 * @param[in] timeout_ms Maximum wait time in milliseconds.
 *
 * @return @ref APP_OK on success, otherwise @ref APP_ETIMEOUT or another error.
 */
app_status_t lan9645_core_soft_reset(lan9645_core_t *ctx, uint32_t timeout_ms);

/**
 * @brief Reads the feature-disable register latched by the current strap mode.
 *
 * @param[in,out] ctx Core context.
 * @param[out] feat_disable Raw feature-disable register value.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_core_read_feat_disable(lan9645_core_t *ctx, uint32_t *feat_disable);

/**
 * @brief Enables or disables one switch port in the forwarding engine.
 *
 * @param[in,out] ctx Core context.
 * @param[in] port Switch port index.
 * @param[in] enable Set to 1 to enable the port, 0 to disable it.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_core_set_port_enable(lan9645_core_t *ctx, uint8_t port, uint8_t enable);

/**
 * @brief Reads the sampled RGMII in-band link state for one external CPU interface.
 *
 * @param[in,out] ctx Core context.
 * @param[in] rgmii_idx RGMII interface index.
 * @param[out] link_up Optional link-up flag.
 * @param[out] duplex Optional duplex flag.
 * @param[out] speed_code Optional speed code extracted from the hardware status bits.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_core_get_rgmii_link(
  lan9645_core_t *ctx,
  uint8_t rgmii_idx,
  uint8_t *link_up,
  uint8_t *duplex,
  uint8_t *speed_code
);

/**
 * @brief Captures the key interrupt registers used by the firmware.
 *
 * @param[in,out] ctx Core context.
 * @param[out] out Destination structure for the IRQ register snapshot.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_core_get_irq_regs(lan9645_core_t *ctx, lan9645_core_irq_regs_t *out);

/**
 * @brief Acknowledges selected ANA interrupt events.
 *
 * @param[in,out] ctx Core context.
 * @param[in] ana_anevents_mask Event mask to acknowledge in ANA_ANEVENTS.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_core_ack_irq(lan9645_core_t *ctx, uint32_t ana_anevents_mask);

#ifdef __cplusplus
}
#endif

#endif /* LAN9645_CORE_H */
