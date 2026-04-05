#include "time/time_service.h"

/**
 * @file time_service.c
 * @brief RTC and uptime helper used by diagnostics, CLI, and status reporting.
 */

#include <stdio.h>
#include <string.h>

#include "main.h"

#define TIME_SERVICE_RTC_VALID_MAGIC (0x54494D45u)
#define TIME_SERVICE_RTC_VALID_BKP_REG RTC_BKP_DR0

extern RTC_HandleTypeDef hrtc;

static uint8_t s_initialized = 0u;
static uint8_t s_rtc_valid = 0u;

/**
 * @brief Performs a lightweight validity check on RTC calendar fields.
 *
 * @param[in] d RTC date structure to validate.
 * @param[in] t RTC time structure to validate.
 *
 * @return 1 when the timestamp looks usable, otherwise 0.
 */
static uint8_t time_service_is_calendar_plausible(const RTC_DateTypeDef *d, const RTC_TimeTypeDef *t)
{
  if ((d == NULL) || (t == NULL)) {
    return 0u;
  }

  if ((d->Month < 1u) || (d->Month > 12u)) {
    return 0u;
  }
  if ((d->Date < 1u) || (d->Date > 31u)) {
    return 0u;
  }
  if (t->Hours > 23u) {
    return 0u;
  }
  if (t->Minutes > 59u) {
    return 0u;
  }
  if (t->Seconds > 59u) {
    return 0u;
  }

  return 1u;
}

/**
 * @brief Initializes cached RTC validity state.
 *
 * @return @ref APP_OK on success.
 */
app_status_t time_service_init(void)
{
  if (s_initialized != 0u) {
    return APP_OK;
  }

  s_rtc_valid = 0u;

  if (hrtc.Instance != NULL) {
    if (HAL_RTCEx_BKUPRead(&hrtc, TIME_SERVICE_RTC_VALID_BKP_REG) == TIME_SERVICE_RTC_VALID_MAGIC) {
      s_rtc_valid = 1u;
    }
  }

  s_initialized = 1u;
  return APP_OK;
}

/**
 * @brief Reads uptime and, when available, a valid RTC timestamp.
 *
 * @param[out] out Destination time snapshot.
 *
 * @return @ref APP_OK on success.
 */
app_status_t time_service_now(app_time_t *out)
{
  RTC_TimeTypeDef sTime = {0};
  RTC_DateTypeDef sDate = {0};

  if (out == NULL) {
    return APP_EINVAL;
  }

  if (s_initialized == 0u) {
    (void)time_service_init();
  }

  memset(out, 0, sizeof(*out));
  out->ms_from_boot = HAL_GetTick();

  if ((s_rtc_valid == 0u) || (hrtc.Instance == NULL)) {
    out->rtc_valid = 0u;
    return APP_OK;
  }

  if (HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN) != HAL_OK) {
    return APP_EIO;
  }
  if (HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN) != HAL_OK) {
    return APP_EIO;
  }

  if (time_service_is_calendar_plausible(&sDate, &sTime) == 0u) {
    out->rtc_valid = 0u;
    return APP_OK;
  }

  out->rtc_valid = 1u;
  out->year = (uint16_t)(2000u + sDate.Year);
  out->month = (uint8_t)sDate.Month;
  out->day = (uint8_t)sDate.Date;
  out->hour = (uint8_t)sTime.Hours;
  out->min = (uint8_t)sTime.Minutes;
  out->sec = (uint8_t)sTime.Seconds;

  return APP_OK;
}

/**
 * @brief Formats a time snapshot for console and diagnostics output.
 *
 * @param[in] t Time snapshot to print.
 * @param[out] buf Output string buffer.
 * @param[in] len Size of @p buf in bytes.
 *
 * @return @ref APP_OK on success.
 */
app_status_t time_service_format(const app_time_t *t, char *buf, uint32_t len)
{
  int n = 0;

  if ((t == NULL) || (buf == NULL) || (len == 0u)) {
    return APP_EINVAL;
  }

  if (t->rtc_valid != 0u) {
    n = snprintf(
      buf,
      (size_t)len,
      "%04u-%02u-%02u %02u:%02u:%02u",
      (unsigned)t->year,
      (unsigned)t->month,
      (unsigned)t->day,
      (unsigned)t->hour,
      (unsigned)t->min,
      (unsigned)t->sec
    );
  } else {
    n = snprintf(buf, (size_t)len, "T+%lums", (unsigned long)t->ms_from_boot);
  }

  if ((n < 0) || ((uint32_t)n >= len)) {
    return APP_EIO;
  }

  return APP_OK;
}
