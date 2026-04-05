#ifndef LAN9645_TARGET_MAP_H
#define LAN9645_TARGET_MAP_H

/**
 * @file lan9645_target_map.h
 * @brief Helpers that load absolute target base addresses into the register layer.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "app_common_types.h"
#include "lan9645/lan9645_reg.h"

/**
 * @brief One target-to-base-address mapping entry.
 */
typedef struct {
  lan9645_target_t target; /**< Register target to map. */
  uint8_t target_instance; /**< Target instance index. */
  uint32_t base_addr;      /**< Absolute base address in LAN9645 word-address units. */
} lan9645_target_map_entry_t;

/**
 * @brief Applies a table of target base-address mappings.
 *
 * @param[in,out] reg Register access layer to update.
 * @param[in] entries Mapping table.
 * @param[in] count Number of entries in @p entries.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_target_map_apply(
  lan9645_reg_t *reg,
  const lan9645_target_map_entry_t *entries,
  uint32_t count
);

/**
 * @brief Applies the Linux MFD-aligned LAN9645 target map used by this project.
 *
 * @param[in,out] reg Register access layer to update.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_target_map_apply_board_defaults(lan9645_reg_t *reg);

#ifdef __cplusplus
}
#endif

#endif /* LAN9645_TARGET_MAP_H */
