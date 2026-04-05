#ifndef LAN9645_PROFILE_MANAGED_RMII1_H
#define LAN9645_PROFILE_MANAGED_RMII1_H

/**
 * @file lan9645_profile_managed_rmii1.h
 * @brief Fixed switch profile for the current managed STM32 RMII design.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "app_common_types.h"
#include "lan9645/lan9645_core.h"

/**
 * @brief Summary of the managed profile state after configuration is applied.
 */
typedef struct {
  uint32_t chip_id;            /**< Raw chip ID captured before or during profile application. */
  uint32_t feat_disable;       /**< Raw feature-disable register value seen for this build. */
  uint32_t ext_cpu_cfg;        /**< Final QSYS_EXT_CPU_CFG value programmed by the profile. */
  uint32_t ports_enabled_mask; /**< Bit mask of switch ports enabled by the profile. */
} lan9645_profile_result_t;

/**
 * @brief Applies the fixed managed-switch profile for STM32 over RMII1.
 *
 * @param[in,out] core Initialized LAN9645 core helper.
 * @param[out] out Optional summary of the applied configuration.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_profile_managed_rmii1_apply(
  lan9645_core_t *core,
  lan9645_profile_result_t *out
);

#ifdef __cplusplus
}
#endif

#endif /* LAN9645_PROFILE_MANAGED_RMII1_H */
