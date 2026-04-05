#ifndef APP_COMMON_TYPES_H
#define APP_COMMON_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef enum {
  APP_OK = 0,
  APP_EINVAL = -1,
  APP_EIO = -2,
  APP_ETIMEOUT = -3,
  APP_ENOTSUP = -4,
  APP_ESTATE = -5
} app_status_t;

typedef enum {
  APP_CH_USB = 0,
  APP_CH_UART4 = 1,
  APP_CH_BOTH = 2
} app_channel_t;

typedef enum {
  APP_OWNER_NONE = 0,
  APP_OWNER_USB = 1,
  APP_OWNER_UART4 = 2
} app_owner_t;

typedef struct {
  uint32_t ms_from_boot;
  uint8_t rtc_valid;
  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t min;
  uint8_t sec;
} app_time_t;

#ifdef __cplusplus
}
#endif

#endif /* APP_COMMON_TYPES_H */
