#ifndef CLI_INGRESS_H
#define CLI_INGRESS_H

/**
 * @file cli_ingress.h
 * @brief Serial ingress queue for USB CDC and UART4 command lines.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "app_common_types.h"

/**
 * @brief Statistics collected while receiving and queueing CLI traffic.
 */
typedef struct {
  uint32_t bytes_usb;            /**< Total bytes received from USB CDC. */
  uint32_t bytes_uart4;          /**< Total bytes received from UART4. */
  uint32_t lines_queued;         /**< Number of complete lines pushed into the queue. */
  uint32_t lines_executed;       /**< Number of queued lines executed by the CLI core. */
  uint32_t line_overflow_count;  /**< Number of input lines dropped for being too long. */
  uint32_t queue_overflow_count; /**< Number of lines dropped because the queue was full. */
  uint32_t command_fail_count;   /**< Number of executed commands that returned failure. */
  uint8_t queue_depth;           /**< Maximum number of queued lines supported. */
  uint8_t queue_count;           /**< Current number of queued lines. */
} cli_ingress_stats_t;

/**
 * @brief Initializes CLI ingress buffers and counters.
 *
 * @return @ref APP_OK on success.
 */
app_status_t cli_ingress_init(void);

/**
 * @brief Feeds a block of received bytes into the CLI ingress parser.
 *
 * @param[in] data Source byte buffer.
 * @param[in] len Number of bytes in @p data.
 * @param[in] source Interface that received the data.
 *
 * @return @ref APP_OK on success, otherwise an application error code.
 */
app_status_t cli_ingress_feed_bytes(const uint8_t *data, uint32_t len, app_channel_t source);

/**
 * @brief Feeds a single received byte into the CLI ingress parser.
 *
 * @param[in] ch Byte to process.
 * @param[in] source Interface that received the byte.
 *
 * @return @ref APP_OK on success, otherwise an application error code.
 */
app_status_t cli_ingress_feed_byte(uint8_t ch, app_channel_t source);

/**
 * @brief Drains queued lines and hands them to the CLI core.
 *
 * @return @ref APP_OK on success.
 */
app_status_t cli_ingress_poll(void);

/**
 * @brief Returns a snapshot of CLI ingress statistics.
 *
 * @param[out] out Destination structure for the statistics snapshot.
 *
 * @return @ref APP_OK on success.
 */
app_status_t cli_ingress_get_stats(cli_ingress_stats_t *out);

#ifdef __cplusplus
}
#endif

#endif /* CLI_INGRESS_H */
