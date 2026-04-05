#include "cli/cli_ingress.h"

/**
 * @file cli_ingress.c
 * @brief Serial ingress queue that turns USB/UART bytes into executable CLI lines.
 */

#include <stdio.h>
#include <string.h>

#include "cli/cli_core.h"
#include "diag/diag_router.h"
#include "main.h"

#define CLI_INGRESS_LINE_LEN          (160u)
#define CLI_INGRESS_QUEUE_DEPTH       (8u)

/**
 * @brief Internal ingress buffer, queue, and statistics state.
 */
typedef struct {
  uint8_t initialized;
  char line_buf[CLI_INGRESS_LINE_LEN];
  uint32_t line_len;
  char queue[CLI_INGRESS_QUEUE_DEPTH][CLI_INGRESS_LINE_LEN];
  app_channel_t queue_source[CLI_INGRESS_QUEUE_DEPTH];
  app_channel_t line_source;
  uint8_t q_head;
  uint8_t q_tail;
  uint8_t q_count;
  cli_ingress_stats_t stats;
} cli_ingress_state_t;

static cli_ingress_state_t s_ing = {0};

/**
 * @brief Enters a short critical section used by the ingress queue.
 *
 * @return Previous PRIMASK state to restore later.
 */
static uint32_t cli_ingress_lock(void)
{
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  return primask;
}

/**
 * @brief Restores the interrupt state saved by @ref cli_ingress_lock.
 *
 * @param[in] primask Saved PRIMASK value.
 */
static void cli_ingress_unlock(uint32_t primask)
{
  if (primask == 0u) {
    __enable_irq();
  }
}

/**
 * @brief Converts an ingress source channel into a diagnostics router owner.
 *
 * @param[in] source Source channel for the received command.
 *
 * @return Preferred diagnostics owner for responses.
 */
static app_owner_t cli_ingress_source_to_owner(app_channel_t source)
{
  if (source == APP_CH_USB) {
    return APP_OWNER_USB;
  }
  if (source == APP_CH_UART4) {
    return APP_OWNER_UART4;
  }
  return APP_OWNER_NONE;
}

/**
 * @brief Updates byte counters for one received character.
 *
 * @param[in] source Channel that supplied the byte.
 */
static void cli_ingress_count_rx_byte_locked(app_channel_t source)
{
  if (source == APP_CH_USB) {
    s_ing.stats.bytes_usb++;
  } else if (source == APP_CH_UART4) {
    s_ing.stats.bytes_uart4++;
  }
}

/**
 * @brief Pushes the current assembled line into the execution queue.
 *
 * @param[in] source Channel that supplied the line.
 */
static void cli_ingress_push_line_locked(app_channel_t source)
{
  if (s_ing.line_len == 0u) {
    return;
  }

  s_ing.line_buf[s_ing.line_len] = '\0';
  if (s_ing.q_count < CLI_INGRESS_QUEUE_DEPTH) {
    (void)snprintf(s_ing.queue[s_ing.q_head], CLI_INGRESS_LINE_LEN, "%s", s_ing.line_buf);
    s_ing.queue_source[s_ing.q_head] = source;
    s_ing.q_head = (uint8_t)((s_ing.q_head + 1u) % CLI_INGRESS_QUEUE_DEPTH);
    s_ing.q_count++;
  } else {
    /* Queue full, drop oldest line and keep latest input. */
    s_ing.stats.queue_overflow_count++;
    s_ing.q_tail = (uint8_t)((s_ing.q_tail + 1u) % CLI_INGRESS_QUEUE_DEPTH);
    (void)snprintf(s_ing.queue[s_ing.q_head], CLI_INGRESS_LINE_LEN, "%s", s_ing.line_buf);
    s_ing.queue_source[s_ing.q_head] = source;
    s_ing.q_head = (uint8_t)((s_ing.q_head + 1u) % CLI_INGRESS_QUEUE_DEPTH);
  }

  s_ing.stats.lines_queued++;
  s_ing.line_len = 0u;
  s_ing.line_source = APP_CH_BOTH;
}

/**
 * @brief Initializes CLI ingress buffers and counters.
 *
 * @return @ref APP_OK on success.
 */
app_status_t cli_ingress_init(void)
{
  memset(&s_ing, 0, sizeof(s_ing));
  s_ing.line_source = APP_CH_BOTH;
  s_ing.stats.queue_depth = CLI_INGRESS_QUEUE_DEPTH;
  s_ing.initialized = 1u;
  return APP_OK;
}

/**
 * @brief Feeds one byte into the line assembler.
 *
 * @param[in] ch Received character.
 * @param[in] source Channel that supplied the character.
 *
 * @return @ref APP_OK on success.
 */
app_status_t cli_ingress_feed_byte(uint8_t ch, app_channel_t source)
{
  uint32_t lock = 0u;

  if (s_ing.initialized == 0u) {
    (void)cli_ingress_init();
  }

  lock = cli_ingress_lock();
  cli_ingress_count_rx_byte_locked(source);

  if (ch == '\r') {
    cli_ingress_unlock(lock);
    return APP_OK;
  }

  if (ch == '\n') {
    cli_ingress_push_line_locked(s_ing.line_source);
    cli_ingress_unlock(lock);
    return APP_OK;
  }

  if ((ch >= 0x20u) && (ch <= 0x7Eu)) {
    if (s_ing.line_len == 0u) {
      s_ing.line_source = source;
    }
    if (s_ing.line_len < (CLI_INGRESS_LINE_LEN - 1u)) {
      s_ing.line_buf[s_ing.line_len++] = (char)ch;
    } else {
      /* Reset on overflow to avoid partial command execution. */
      s_ing.line_len = 0u;
      s_ing.line_source = APP_CH_BOTH;
      s_ing.stats.line_overflow_count++;
    }
  }

  cli_ingress_unlock(lock);
  return APP_OK;
}

/**
 * @brief Feeds a block of bytes into the line assembler.
 *
 * @param[in] data Input byte buffer.
 * @param[in] len Number of bytes in @p data.
 * @param[in] source Channel that supplied the buffer.
 *
 * @return @ref APP_OK on success.
 */
app_status_t cli_ingress_feed_bytes(const uint8_t *data, uint32_t len, app_channel_t source)
{
  uint32_t i = 0u;

  if ((data == NULL) && (len > 0u)) {
    return APP_EINVAL;
  }
  if (s_ing.initialized == 0u) {
    (void)cli_ingress_init();
  }

  for (i = 0u; i < len; ++i) {
    (void)cli_ingress_feed_byte(data[i], source);
  }

  return APP_OK;
}

/**
 * @brief Drains queued lines and dispatches them to the CLI core.
 *
 * @return @ref APP_OK on success.
 */
app_status_t cli_ingress_poll(void)
{
  char line[CLI_INGRESS_LINE_LEN];
  app_channel_t source = APP_CH_BOTH;
  uint32_t lock = 0u;

  if (s_ing.initialized == 0u) {
    (void)cli_ingress_init();
  }

  while (1) {
    lock = cli_ingress_lock();
    if (s_ing.q_count == 0u) {
      cli_ingress_unlock(lock);
      break;
    }

    (void)snprintf(line, sizeof(line), "%s", s_ing.queue[s_ing.q_tail]);
    source = s_ing.queue_source[s_ing.q_tail];
    s_ing.q_tail = (uint8_t)((s_ing.q_tail + 1u) % CLI_INGRESS_QUEUE_DEPTH);
    s_ing.q_count--;
    s_ing.stats.queue_count = s_ing.q_count;
    cli_ingress_unlock(lock);

    (void)diag_router_set_owner(cli_ingress_source_to_owner(source));
    s_ing.stats.lines_executed++;
    if (cli_core_exec_line(line) != APP_OK) {
      s_ing.stats.command_fail_count++;
      (void)diag_router_write_text("cli: command failed\r\n");
    }
  }

  return APP_OK;
}

/**
 * @brief Returns a snapshot of ingress statistics.
 *
 * @param[out] out Destination statistics snapshot.
 *
 * @return @ref APP_OK on success.
 */
app_status_t cli_ingress_get_stats(cli_ingress_stats_t *out)
{
  uint32_t lock = 0u;

  if (out == NULL) {
    return APP_EINVAL;
  }
  if (s_ing.initialized == 0u) {
    (void)cli_ingress_init();
  }

  lock = cli_ingress_lock();
  s_ing.stats.queue_count = s_ing.q_count;
  *out = s_ing.stats;
  cli_ingress_unlock(lock);
  return APP_OK;
}
