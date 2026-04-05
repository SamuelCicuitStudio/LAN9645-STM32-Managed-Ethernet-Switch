#ifndef TIME_SERVICE_H
#define TIME_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "app_common_types.h"

app_status_t time_service_init(void);
app_status_t time_service_now(app_time_t *out);
app_status_t time_service_format(const app_time_t *t, char *buf, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* TIME_SERVICE_H */
