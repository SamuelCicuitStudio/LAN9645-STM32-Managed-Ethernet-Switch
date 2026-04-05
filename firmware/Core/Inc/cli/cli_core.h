#ifndef CLI_CORE_H
#define CLI_CORE_H

/**
 * @file cli_core.h
 * @brief Command parser and dispatcher for the bring-up CLI.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "app_common_types.h"

/**
 * @brief Runtime counters exposed by the CLI core.
 */
typedef struct {
  uint8_t initialized;         /**< Set after the CLI core has been initialized. */
  uint8_t active_port;         /**< Currently selected LAN port for port-scoped commands. */
  uint32_t command_count;      /**< Number of parsed commands accepted by the CLI core. */
  uint32_t command_error_count;/**< Number of commands that returned an error. */
} cli_core_state_t;

/**
 * @brief Initializes the command parser, built-in commands, and CLI state.
 *
 * @return @ref APP_OK on success.
 */
app_status_t cli_core_init(void);

/**
 * @brief Runs deferred CLI work such as line execution and background updates.
 *
 * @return @ref APP_OK on success.
 */
app_status_t cli_core_poll(void);

/**
 * @brief Parses and executes one complete CLI command line.
 *
 * @param[in] line Zero-terminated command line without transport framing.
 *
 * @return @ref APP_OK on success, otherwise the command's failure code.
 */
app_status_t cli_core_exec_line(const char *line);

/**
 * @brief Returns a snapshot of the CLI core state counters.
 *
 * @param[out] out Destination structure for the state snapshot.
 *
 * @return @ref APP_OK on success.
 */
app_status_t cli_core_get_state(cli_core_state_t *out);

#ifdef __cplusplus
}
#endif

#endif /* CLI_CORE_H */
