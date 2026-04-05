#include "lan9645/lan9645_target_map.h"

/**
 * @file lan9645_target_map.c
 * @brief Target-base address table for the active LAN9645 STM32 integration.
 */

#include <stddef.h>

#define LAN9645_TGT_BASE_QS        (0x00008000u)
#define LAN9645_TGT_BASE_CHIP_TOP  (0x00010000u)
#define LAN9645_TGT_BASE_REW       (0x00018000u)
#define LAN9645_TGT_BASE_SYS       (0x0002C000u)
#define LAN9645_TGT_BASE_DEV0      (0x00038000u)
#define LAN9645_TGT_BASE_DEV_STEP  (0x00004000u)
#define LAN9645_TGT_BASE_QSYS      (0x00100000u)
#define LAN9645_TGT_BASE_AFI       (0x00120000u)
#define LAN9645_TGT_BASE_ANA       (0x00140000u)
#define LAN9645_TGT_BASE_CPU       (0x00520000u)
#define LAN9645_TGT_BASE_GCB       (0x00004000u)
#define LAN9645_TGT_BASE_HSIO      (0x00030000u)

/**
 * @brief Default target map aligned with the Linux MFD LAN9645 address model.
 */
static const lan9645_target_map_entry_t s_board_target_map[] = {
  { LAN9645_TARGET_GCB, 0u, LAN9645_TGT_BASE_GCB },
  { LAN9645_TARGET_QS, 0u, LAN9645_TGT_BASE_QS },
  { LAN9645_TARGET_HSIO, 0u, LAN9645_TGT_BASE_HSIO },
  { LAN9645_TARGET_CHIP_TOP, 0u, LAN9645_TGT_BASE_CHIP_TOP },
  { LAN9645_TARGET_REW, 0u, LAN9645_TGT_BASE_REW },
  { LAN9645_TARGET_SYS, 0u, LAN9645_TGT_BASE_SYS },
  { LAN9645_TARGET_DEV, 0u, LAN9645_TGT_BASE_DEV0 + (0u * LAN9645_TGT_BASE_DEV_STEP) },
  { LAN9645_TARGET_DEV, 1u, LAN9645_TGT_BASE_DEV0 + (1u * LAN9645_TGT_BASE_DEV_STEP) },
  { LAN9645_TARGET_DEV, 2u, LAN9645_TGT_BASE_DEV0 + (2u * LAN9645_TGT_BASE_DEV_STEP) },
  { LAN9645_TARGET_DEV, 3u, LAN9645_TGT_BASE_DEV0 + (3u * LAN9645_TGT_BASE_DEV_STEP) },
  { LAN9645_TARGET_DEV, 4u, LAN9645_TGT_BASE_DEV0 + (4u * LAN9645_TGT_BASE_DEV_STEP) },
  { LAN9645_TARGET_DEV, 5u, LAN9645_TGT_BASE_DEV0 + (5u * LAN9645_TGT_BASE_DEV_STEP) },
  { LAN9645_TARGET_DEV, 6u, LAN9645_TGT_BASE_DEV0 + (6u * LAN9645_TGT_BASE_DEV_STEP) },
  { LAN9645_TARGET_DEV, 7u, LAN9645_TGT_BASE_DEV0 + (7u * LAN9645_TGT_BASE_DEV_STEP) },
  { LAN9645_TARGET_DEV, 8u, LAN9645_TGT_BASE_DEV0 + (8u * LAN9645_TGT_BASE_DEV_STEP) },
  { LAN9645_TARGET_QSYS, 0u, LAN9645_TGT_BASE_QSYS },
  { LAN9645_TARGET_AFI, 0u, LAN9645_TGT_BASE_AFI },
  { LAN9645_TARGET_ANA, 0u, LAN9645_TGT_BASE_ANA },
  { LAN9645_TARGET_CPU, 0u, LAN9645_TGT_BASE_CPU }
};

/**
 * @brief Applies a target base-address table to the register layer.
 *
 * @param[in,out] reg Register layer to update.
 * @param[in] entries Mapping entries to apply.
 * @param[in] count Number of mapping entries.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_target_map_apply(
  lan9645_reg_t *reg,
  const lan9645_target_map_entry_t *entries,
  uint32_t count
)
{
  uint32_t i = 0u;
  app_status_t st = APP_OK;

  if ((reg == NULL) || (entries == NULL)) {
    return APP_EINVAL;
  }
  if (count == 0u) {
    return APP_EINVAL;
  }

  for (i = 0u; i < count; ++i) {
    st = lan9645_reg_set_target_base(
      reg,
      entries[i].target,
      entries[i].target_instance,
      entries[i].base_addr
    );
    if (st != APP_OK) {
      return st;
    }
  }

  return APP_OK;
}

/**
 * @brief Applies the project's default target map.
 *
 * @param[in,out] reg Register layer to update.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_target_map_apply_board_defaults(lan9645_reg_t *reg)
{
  return lan9645_target_map_apply(
    reg,
    s_board_target_map,
    (uint32_t)(sizeof(s_board_target_map) / sizeof(s_board_target_map[0]))
  );
}
