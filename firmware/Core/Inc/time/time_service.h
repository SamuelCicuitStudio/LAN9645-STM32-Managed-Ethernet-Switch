#ifndef TIME_SERVICE_H
#define TIME_SERVICE_H

/**
 * @file time_service.h
 * @brief Small wrapper around uptime and RTC access used by diagnostics and CLI.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "app_common_types.h"

/**
 * @brief Initializes the time service and validates RTC availability.
 *
 * @return @ref APP_OK when the service is ready.
 */
app_status_t time_service_init(void);

/**
 * @brief Reads the current uptime and RTC snapshot.
 *
 * @param[out] out Destination structure that receives the current time snapshot.
 *
 * @return @ref APP_OK on success, otherwise an application error code.
 */
app_status_t time_service_now(app_time_t *out);

/**
 * @brief Formats a time snapshot into a printable string.
 *
 * @param[in] t Time snapshot to format.
 * @param[out] buf Destination buffer for the formatted string.
 * @param[in] len Size of @p buf in bytes.
 *
 * @return @ref APP_OK on success, otherwise an application error code.
 */
app_status_t time_service_format(const app_time_t *t, char *buf, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* TIME_SERVICE_H */
