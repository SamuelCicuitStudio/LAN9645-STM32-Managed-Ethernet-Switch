#ifndef LAN9645_PROFILE_MANAGED_RMII1_H
#define LAN9645_PROFILE_MANAGED_RMII1_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "app_common_types.h"
#include "lan9645/lan9645_core.h"

typedef struct {
  uint32_t feat_disable;
} lan9645_profile_result_t;

app_status_t lan9645_profile_managed_rmii1_apply(
  lan9645_core_t *core,
  lan9645_profile_result_t *out
);

#ifdef __cplusplus
}
#endif

#endif /* LAN9645_PROFILE_MANAGED_RMII1_H */
