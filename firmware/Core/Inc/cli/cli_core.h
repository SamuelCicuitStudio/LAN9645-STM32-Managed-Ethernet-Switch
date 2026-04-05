#ifndef CLI_CORE_H
#define CLI_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "app_common_types.h"

typedef struct {
  uint8_t initialized;
  uint8_t active_port;
  uint32_t command_count;
  uint32_t command_error_count;
} cli_core_state_t;

app_status_t cli_core_init(void);
app_status_t cli_core_poll(void);
app_status_t cli_core_exec_line(const char *line);
app_status_t cli_core_get_state(cli_core_state_t *out);

#ifdef __cplusplus
}
#endif

#endif /* CLI_CORE_H */
