#ifndef LAN9645_TARGET_MAP_H
#define LAN9645_TARGET_MAP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "app_common_types.h"
#include "lan9645/lan9645_reg.h"

typedef struct {
  lan9645_target_t target;
  uint8_t target_instance;
  uint32_t base_addr;
} lan9645_target_map_entry_t;

app_status_t lan9645_target_map_apply(
  lan9645_reg_t *reg,
  const lan9645_target_map_entry_t *entries,
  uint32_t count
);

/*
 * Applies board defaults for target base addresses.
 * These offsets are provisional and must be verified against the
 * final LAN9645 memory map used by your hardware integration.
 */
app_status_t lan9645_target_map_apply_board_defaults(lan9645_reg_t *reg);

#ifdef __cplusplus
}
#endif

#endif /* LAN9645_TARGET_MAP_H */
