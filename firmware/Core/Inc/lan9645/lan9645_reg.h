#ifndef LAN9645_REG_H
#define LAN9645_REG_H

/**
 * @file lan9645_reg.h
 * @brief Register abstraction built on top of the raw LAN9645 SPI transactions.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "app_common_types.h"
#include "lan9645/lan9645_regs.h"
#include "lan9645/lan9645_spi.h"

/**
 * @brief Register access context with resolved base addresses for active targets.
 */
typedef struct {
  lan9645_spi_t *spi;                            /**< Raw SPI transport used for all register access. */
  uint32_t target_base[LAN9645_TARGET_COUNT];    /**< Base address table indexed by @ref lan9645_target_t. */
  uint8_t target_base_valid[LAN9645_TARGET_COUNT]; /**< Valid flag for each entry in @ref target_base. */
  uint8_t initialized;                           /**< Set after the register layer has been initialized. */
} lan9645_reg_t;

/**
 * @brief Initializes the register access layer.
 *
 * @param[out] ctx Register context to initialize.
 * @param[in] spi Raw SPI transport already configured for the switch.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_reg_init(lan9645_reg_t *ctx, lan9645_spi_t *spi);

/**
 * @brief Installs one resolved base address for a register target and instance.
 *
 * @param[in,out] ctx Register context.
 * @param[in] target Register target identifier.
 * @param[in] target_instance Target instance index.
 * @param[in] base_addr Absolute word address base for the target instance.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_reg_set_target_base(
  lan9645_reg_t *ctx,
  lan9645_target_t target,
  uint8_t target_instance,
  uint32_t base_addr
);

/**
 * @brief Clears one resolved target base address entry.
 *
 * @param[in,out] ctx Register context.
 * @param[in] target Register target identifier.
 * @param[in] target_instance Target instance index.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_reg_clear_target_base(
  lan9645_reg_t *ctx,
  lan9645_target_t target,
  uint8_t target_instance
);

/**
 * @brief Returns the resolved base address for one target instance.
 *
 * @param[in,out] ctx Register context.
 * @param[in] target Register target identifier.
 * @param[in] target_instance Target instance index.
 * @param[out] base_addr Destination for the absolute base address.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_reg_get_target_base(
  lan9645_reg_t *ctx,
  lan9645_target_t target,
  uint8_t target_instance,
  uint32_t *base_addr
);

/**
 * @brief Reads one 32-bit register using an already-resolved absolute word address.
 *
 * @param[in,out] ctx Register context.
 * @param[in] reg_addr Absolute word address.
 * @param[out] value Destination for the register value.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_reg_read32(lan9645_reg_t *ctx, uint32_t reg_addr, uint32_t *value);

/**
 * @brief Writes one 32-bit register using an already-resolved absolute word address.
 *
 * @param[in,out] ctx Register context.
 * @param[in] reg_addr Absolute word address.
 * @param[in] value Register value to write.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_reg_write32(lan9645_reg_t *ctx, uint32_t reg_addr, uint32_t value);

/**
 * @brief Performs a read-modify-write on a 32-bit register.
 *
 * @param[in,out] ctx Register context.
 * @param[in] reg_addr Absolute word address.
 * @param[in] mask Bit mask selecting the field to update.
 * @param[in] value New masked value to merge into the register.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_reg_rmw32(lan9645_reg_t *ctx, uint32_t reg_addr, uint32_t mask, uint32_t value);

/**
 * @brief Polls a register field until it matches an expected value.
 *
 * @param[in,out] ctx Register context.
 * @param[in] reg_addr Absolute word address.
 * @param[in] mask Field mask to test.
 * @param[in] expected Expected masked value.
 * @param[in] timeout_ms Maximum poll time in milliseconds.
 * @param[out] last_read Optional copy of the last register value read.
 *
 * @return @ref APP_OK on success, otherwise @ref APP_ETIMEOUT or another error.
 */
app_status_t lan9645_reg_wait_field(
  lan9645_reg_t *ctx,
  uint32_t reg_addr,
  uint32_t mask,
  uint32_t expected,
  uint32_t timeout_ms,
  uint32_t *last_read
);

/**
 * @brief Reads a register described by a generated register descriptor.
 *
 * @param[in,out] ctx Register context.
 * @param[in] desc Register descriptor.
 * @param[in] target_instance Target instance offset.
 * @param[in] group_instance Group instance offset.
 * @param[in] reg_instance Register instance offset.
 * @param[out] value Destination for the register value.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_reg_read32_desc(
  lan9645_reg_t *ctx,
  const lan9645_reg_desc_t *desc,
  uint32_t target_instance,
  uint32_t group_instance,
  uint32_t reg_instance,
  uint32_t *value
);

/**
 * @brief Writes a register described by a generated register descriptor.
 *
 * @param[in,out] ctx Register context.
 * @param[in] desc Register descriptor.
 * @param[in] target_instance Target instance offset.
 * @param[in] group_instance Group instance offset.
 * @param[in] reg_instance Register instance offset.
 * @param[in] value Register value to write.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_reg_write32_desc(
  lan9645_reg_t *ctx,
  const lan9645_reg_desc_t *desc,
  uint32_t target_instance,
  uint32_t group_instance,
  uint32_t reg_instance,
  uint32_t value
);

/**
 * @brief Performs a read-modify-write using a generated register descriptor.
 *
 * @param[in,out] ctx Register context.
 * @param[in] desc Register descriptor.
 * @param[in] target_instance Target instance offset.
 * @param[in] group_instance Group instance offset.
 * @param[in] reg_instance Register instance offset.
 * @param[in] mask Field mask to update.
 * @param[in] value New masked value to merge into the register.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_reg_rmw32_desc(
  lan9645_reg_t *ctx,
  const lan9645_reg_desc_t *desc,
  uint32_t target_instance,
  uint32_t group_instance,
  uint32_t reg_instance,
  uint32_t mask,
  uint32_t value
);

/**
 * @brief Polls a descriptor-based register field until it matches an expected value.
 *
 * @param[in,out] ctx Register context.
 * @param[in] desc Register descriptor.
 * @param[in] target_instance Target instance offset.
 * @param[in] group_instance Group instance offset.
 * @param[in] reg_instance Register instance offset.
 * @param[in] mask Field mask to test.
 * @param[in] expected Expected masked value.
 * @param[in] timeout_ms Maximum poll time in milliseconds.
 * @param[out] last_read Optional copy of the last register value read.
 *
 * @return @ref APP_OK on success, otherwise @ref APP_ETIMEOUT or another error.
 */
app_status_t lan9645_reg_wait32_desc(
  lan9645_reg_t *ctx,
  const lan9645_reg_desc_t *desc,
  uint32_t target_instance,
  uint32_t group_instance,
  uint32_t reg_instance,
  uint32_t mask,
  uint32_t expected,
  uint32_t timeout_ms,
  uint32_t *last_read
);

#ifdef __cplusplus
}
#endif

#endif /* LAN9645_REG_H */
