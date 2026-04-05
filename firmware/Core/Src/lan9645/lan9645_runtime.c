#include "lan9645/lan9645_runtime.h"

static lan9645_core_t *s_lan_core = NULL;

void lan9645_runtime_set_core(lan9645_core_t *core)
{
  s_lan_core = core;
}

lan9645_core_t *lan9645_runtime_get_core(void)
{
  return s_lan_core;
}
