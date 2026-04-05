#ifndef DIAG_CORE_H
#define DIAG_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "app_common_types.h"

typedef enum {
  DIAG_SEV_INFO = 0,
  DIAG_SEV_WARN = 1,
  DIAG_SEV_ERR = 2,
  DIAG_SEV_FATAL = 3
} diag_severity_t;

typedef enum {
  DIAG_EVT_BOOT = 0,
  DIAG_EVT_LAN_SPI_IFSTAT,
  DIAG_EVT_LAN_SPI_TIMEOUT,
  DIAG_EVT_LAN_STRAP_EXPECTATION,
  DIAG_EVT_LAN_RMII_LINK_TIMEOUT,
  DIAG_EVT_LAN_SERDES0_LINK_TIMEOUT,
  DIAG_EVT_LAN_SERDES1_LINK_TIMEOUT,
  DIAG_EVT_LAN_IRQ_STUCK,
  DIAG_EVT_LAN_DROP_COUNTER_ALERT,
  DIAG_EVT_COUNT
} diag_event_id_t;

typedef struct {
  diag_event_id_t id;
  diag_severity_t severity;
  uint32_t dev;
  uint32_t reg;
  uint32_t arg0;
  uint32_t arg1;
} diag_event_t;

typedef struct {
  diag_event_id_t id;
  uint32_t count;
  app_time_t first_ts;
  app_time_t last_ts;
  uint32_t last_dev;
  uint32_t last_reg;
  uint32_t last_arg0;
  uint32_t last_arg1;
} diag_error_counter_t;

app_status_t diag_core_init(void);
app_status_t diag_core_report(const diag_event_t *evt);
app_status_t diag_core_get_error(diag_event_id_t id, diag_error_counter_t *out);
app_status_t diag_core_get_last(diag_event_t *out);
app_status_t diag_core_clear_all(void);

#ifdef __cplusplus
}
#endif

#endif /* DIAG_CORE_H */
