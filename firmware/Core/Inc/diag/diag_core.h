#ifndef DIAG_CORE_H
#define DIAG_CORE_H

/**
 * @file diag_core.h
 * @brief Persistent in-memory diagnostics event store and counters.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "app_common_types.h"

/**
 * @brief Severity attached to a diagnostics event.
 */
typedef enum {
  DIAG_SEV_INFO = 0,
  DIAG_SEV_WARN = 1,
  DIAG_SEV_ERR = 2,
  DIAG_SEV_FATAL = 3
} diag_severity_t;

/**
 * @brief Stable identifiers for the diagnostics events we care about at bring-up.
 */
typedef enum {
  DIAG_EVT_BOOT = 0,
  DIAG_EVT_LAN_SPI_IFSTAT,
  DIAG_EVT_LAN_SPI_TIMEOUT,
  DIAG_EVT_LAN_PORT_CFG_FAIL,
  DIAG_EVT_LAN_VLAN_CFG_FAIL,
  DIAG_EVT_LAN_CNT_OP_FAIL,
  DIAG_EVT_LAN_STRAP_EXPECTATION,
  DIAG_EVT_LAN_RMII_LINK_TIMEOUT,
  DIAG_EVT_LAN_SERDES0_LINK_TIMEOUT,
  DIAG_EVT_LAN_SERDES1_LINK_TIMEOUT,
  DIAG_EVT_LAN_IRQ_STUCK,
  DIAG_EVT_LAN_IRQ_CAUSE,
  DIAG_EVT_LAN_IRQ_ACK_FAIL,
  DIAG_EVT_LAN_DROP_COUNTER_ALERT,
  DIAG_EVT_COUNT
} diag_event_id_t;

/**
 * @brief One diagnostics event reported by a service at runtime.
 */
typedef struct {
  diag_event_id_t id;      /**< Event identifier. */
  diag_severity_t severity;/**< Event severity level. */
  uint32_t dev;            /**< Optional device or port identifier. */
  uint32_t reg;            /**< Optional register address associated with the event. */
  uint32_t arg0;           /**< Event-specific context word 0. */
  uint32_t arg1;           /**< Event-specific context word 1. */
} diag_event_t;

/**
 * @brief Aggregated error information kept for a diagnostics event ID.
 */
typedef struct {
  diag_event_id_t id;   /**< Event identifier tracked by this counter entry. */
  uint32_t count;       /**< Number of times the event has been reported. */
  app_time_t first_ts;  /**< Timestamp captured on the first occurrence. */
  app_time_t last_ts;   /**< Timestamp captured on the most recent occurrence. */
  uint32_t last_dev;    /**< Last reported device or port identifier. */
  uint32_t last_reg;    /**< Last reported register address. */
  uint32_t last_arg0;   /**< Last event-specific context word 0. */
  uint32_t last_arg1;   /**< Last event-specific context word 1. */
} diag_error_counter_t;

/**
 * @brief Clears diagnostics state and prepares the event store.
 *
 * @return @ref APP_OK when initialization succeeds.
 */
app_status_t diag_core_init(void);

/**
 * @brief Records a diagnostics event and updates the matching counters.
 *
 * @param[in] evt Event to store.
 *
 * @return @ref APP_OK on success, otherwise an application error code.
 */
app_status_t diag_core_report(const diag_event_t *evt);

/**
 * @brief Returns the accumulated counter for one diagnostics event ID.
 *
 * @param[in] id Event identifier to query.
 * @param[out] out Destination structure for the counter snapshot.
 *
 * @return @ref APP_OK on success, otherwise an application error code.
 */
app_status_t diag_core_get_error(diag_event_id_t id, diag_error_counter_t *out);

/**
 * @brief Returns the last event reported to the diagnostics core.
 *
 * @param[out] out Destination structure for the last event snapshot.
 *
 * @return @ref APP_OK on success, otherwise an application error code.
 */
app_status_t diag_core_get_last(diag_event_t *out);

/**
 * @brief Removes all stored diagnostics events and counters.
 *
 * @return @ref APP_OK on success.
 */
app_status_t diag_core_clear_all(void);

#ifdef __cplusplus
}
#endif

#endif /* DIAG_CORE_H */
