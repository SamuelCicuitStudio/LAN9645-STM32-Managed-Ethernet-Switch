#ifndef DIAG_ROUTER_H
#define DIAG_ROUTER_H

/**
 * @file diag_router.h
 * @brief Routes diagnostic text and events to USB CDC and/or UART4.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "app_common_types.h"
#include "diag/diag_core.h"

/**
 * @brief Snapshot of the current diagnostics routing policy and link readiness.
 */
typedef struct {
  app_owner_t owner;        /**< Preferred output owner for replies and events. */
  uint8_t usb_ready;        /**< Set when USB CDC transport is ready to transmit. */
  uint8_t uart_ready;       /**< Set when UART4 transport is ready to transmit. */
  uint8_t mirror_when_none; /**< Set when output is mirrored while no owner is locked. */
  uint8_t bridge_usb_uart_on; /**< Set when traffic is forwarded between both serial links. */
} diag_router_state_t;

/**
 * @brief Initializes the diagnostics routing layer.
 *
 * @return @ref APP_OK on success.
 */
app_status_t diag_router_init(void);

/**
 * @brief Selects which serial interface owns outgoing text by default.
 *
 * @param[in] owner Requested diagnostics owner.
 *
 * @return @ref APP_OK on success.
 */
app_status_t diag_router_set_owner(app_owner_t owner);

/**
 * @brief Returns the currently selected diagnostics owner.
 *
 * @return Current owner selection.
 */
app_owner_t diag_router_get_owner(void);

/**
 * @brief Updates USB CDC readiness used by the router.
 *
 * @param[in] ready Set to 1 when USB CDC can accept output.
 *
 * @return @ref APP_OK on success.
 */
app_status_t diag_router_set_usb_ready(uint8_t ready);

/**
 * @brief Updates UART4 readiness used by the router.
 *
 * @param[in] ready Set to 1 when UART4 can accept output.
 *
 * @return @ref APP_OK on success.
 */
app_status_t diag_router_set_uart_ready(uint8_t ready);

/**
 * @brief Writes one text line through the active diagnostics route.
 *
 * @param[in] line Zero-terminated string to send.
 *
 * @return @ref APP_OK on success, otherwise an application error code.
 */
app_status_t diag_router_write_text(const char *line);

/**
 * @brief Formats and forwards one diagnostics event through the router.
 *
 * @param[in] evt Event to print.
 *
 * @return @ref APP_OK on success, otherwise an application error code.
 */
app_status_t diag_router_write_event(const diag_event_t *evt);

/**
 * @brief Enables or disables live forwarding between USB CDC and UART4.
 *
 * @param[in] on Set to 1 to enable bridging, 0 to disable it.
 *
 * @return @ref APP_OK on success.
 */
app_status_t diag_router_set_bridge(uint8_t on);

/**
 * @brief Returns the current diagnostics router snapshot.
 *
 * @param[out] out Destination structure for the state snapshot.
 *
 * @return @ref APP_OK on success.
 */
app_status_t diag_router_get_state(diag_router_state_t *out);

/**
 * @brief Performs periodic router maintenance.
 *
 * @return @ref APP_OK on success.
 */
app_status_t diag_router_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* DIAG_ROUTER_H */
