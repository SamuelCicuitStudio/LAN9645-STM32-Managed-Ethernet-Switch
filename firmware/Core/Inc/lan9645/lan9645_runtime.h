#ifndef LAN9645_RUNTIME_H
#define LAN9645_RUNTIME_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lan9645/lan9645_core.h"

void lan9645_runtime_set_core(lan9645_core_t *core);
lan9645_core_t *lan9645_runtime_get_core(void);

#ifdef __cplusplus
}
#endif

#endif /* LAN9645_RUNTIME_H */
