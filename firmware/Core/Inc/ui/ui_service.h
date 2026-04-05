#ifndef UI_SERVICE_H
#define UI_SERVICE_H

/**
 * @file ui_service.h
 * @brief Board UI helper for LEDs and the user push button.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "app_common_types.h"

/**
 * @brief Initializes the UI service and drives LEDs to a known state.
 *
 * @return @ref APP_OK on success.
 */
app_status_t ui_service_init(void);

/**
 * @brief Updates button debounce and LED timing state.
 *
 * @return @ref APP_OK on success.
 */
app_status_t ui_service_poll(void);

/**
 * @brief Controls the STATUS LED.
 *
 * @param[in] on Set to 1 to drive the LED high, 0 to turn it off.
 *
 * @return @ref APP_OK on success.
 */
app_status_t ui_service_set_status(uint8_t on);

/**
 * @brief Controls the ERR LED.
 *
 * @param[in] on Set to 1 to drive the LED high, 0 to turn it off.
 *
 * @return @ref APP_OK on success.
 */
app_status_t ui_service_set_error(uint8_t on);

/**
 * @brief Controls the HEART/RUN LED.
 *
 * @param[in] on Set to 1 to drive the LED high, 0 to turn it off.
 *
 * @return @ref APP_OK on success.
 */
app_status_t ui_service_set_run(uint8_t on);

/**
 * @brief Returns and clears the debounced user-button event latch.
 *
 * @return 1 when a new button press event was captured, otherwise 0.
 */
uint8_t ui_service_take_button_event(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_SERVICE_H */
