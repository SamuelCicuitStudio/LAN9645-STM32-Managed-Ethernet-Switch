#ifndef APP_COMMON_TYPES_H
#define APP_COMMON_TYPES_H

/**
 * @file app_common_types.h
 * @brief Common status, routing, and timestamp types shared by the firmware.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief Generic application status codes returned by custom services.
 */
typedef enum {
  APP_OK = 0,
  APP_EINVAL = -1,
  APP_EIO = -2,
  APP_ETIMEOUT = -3,
  APP_ENOTSUP = -4,
  APP_ESTATE = -5
} app_status_t;

/**
 * @brief Input or output channel used by the CLI and diagnostics router.
 */
typedef enum {
  APP_CH_USB = 0,
  APP_CH_UART4 = 1,
  APP_CH_BOTH = 2
} app_channel_t;

/**
 * @brief Preferred serial owner when one console should receive replies first.
 */
typedef enum {
  APP_OWNER_NONE = 0,
  APP_OWNER_USB = 1,
  APP_OWNER_UART4 = 2
} app_owner_t;

/**
 * @brief Normalized wall-clock and uptime snapshot used in logs and state dumps.
 */
typedef struct {
  uint32_t ms_from_boot; /**< Milliseconds elapsed since MCU boot. */
  uint8_t rtc_valid;     /**< Set to 1 when the RTC date/time fields are valid. */
  uint16_t year;         /**< Calendar year when @ref rtc_valid is set. */
  uint8_t month;         /**< Calendar month in range 1..12. */
  uint8_t day;           /**< Calendar day in range 1..31. */
  uint8_t hour;          /**< Hours in 24-hour format. */
  uint8_t min;           /**< Minutes in range 0..59. */
  uint8_t sec;           /**< Seconds in range 0..59. */
} app_time_t;

#ifdef __cplusplus
}
#endif

#endif /* APP_COMMON_TYPES_H */
