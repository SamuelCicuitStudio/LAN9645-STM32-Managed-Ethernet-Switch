#include "cli/cli_ingress.h"

#include <stdio.h>
#include <string.h>

#include "cli/cli_core.h"
#include "diag/diag_router.h"
#include "main.h"

#define CLI_INGRESS_LINE_LEN          (160u)
#define CLI_INGRESS_QUEUE_DEPTH       (8u)

typedef struct {
  uint8_t initialized;
  char line_buf[CLI_INGRESS_LINE_LEN];
  uint32_t line_len;
  char queue[CLI_INGRESS_QUEUE_DEPTH][CLI_INGRESS_LINE_LEN];
  uint8_t q_head;
  uint8_t q_tail;
  uint8_t q_count;
} cli_ingress_state_t;

static cli_ingress_state_t s_ing = {0};

static uint32_t cli_ingress_lock(void)
{
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  return primask;
}

static void cli_ingress_unlock(uint32_t primask)
{
  if (primask == 0u) {
    __enable_irq();
  }
}

static void cli_ingress_push_line_locked(void)
{
  if (s_ing.line_len == 0u) {
    return;
  }

  s_ing.line_buf[s_ing.line_len] = '\0';
  if (s_ing.q_count < CLI_INGRESS_QUEUE_DEPTH) {
    (void)snprintf(s_ing.queue[s_ing.q_head], CLI_INGRESS_LINE_LEN, "%s", s_ing.line_buf);
    s_ing.q_head = (uint8_t)((s_ing.q_head + 1u) % CLI_INGRESS_QUEUE_DEPTH);
    s_ing.q_count++;
  } else {
    /* Queue full, drop oldest line and keep latest input. */
    s_ing.q_tail = (uint8_t)((s_ing.q_tail + 1u) % CLI_INGRESS_QUEUE_DEPTH);
    (void)snprintf(s_ing.queue[s_ing.q_head], CLI_INGRESS_LINE_LEN, "%s", s_ing.line_buf);
    s_ing.q_head = (uint8_t)((s_ing.q_head + 1u) % CLI_INGRESS_QUEUE_DEPTH);
  }

  s_ing.line_len = 0u;
}

app_status_t cli_ingress_init(void)
{
  memset(&s_ing, 0, sizeof(s_ing));
  s_ing.initialized = 1u;
  return APP_OK;
}

app_status_t cli_ingress_feed_byte(uint8_t ch, app_channel_t source)
{
  uint32_t lock = 0u;
  (void)source;

  if (s_ing.initialized == 0u) {
    (void)cli_ingress_init();
  }

  lock = cli_ingress_lock();

  if (ch == '\r') {
    cli_ingress_unlock(lock);
    return APP_OK;
  }

  if (ch == '\n') {
    cli_ingress_push_line_locked();
    cli_ingress_unlock(lock);
    return APP_OK;
  }

  if ((ch >= 0x20u) && (ch <= 0x7Eu)) {
    if (s_ing.line_len < (CLI_INGRESS_LINE_LEN - 1u)) {
      s_ing.line_buf[s_ing.line_len++] = (char)ch;
    } else {
      /* Reset on overflow to avoid partial command execution. */
      s_ing.line_len = 0u;
    }
  }

  cli_ingress_unlock(lock);
  return APP_OK;
}

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

app_status_t cli_ingress_poll(void)
{
  char line[CLI_INGRESS_LINE_LEN];
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
    s_ing.q_tail = (uint8_t)((s_ing.q_tail + 1u) % CLI_INGRESS_QUEUE_DEPTH);
    s_ing.q_count--;
    cli_ingress_unlock(lock);

    if (cli_core_exec_line(line) != APP_OK) {
      (void)diag_router_write_text("cli: command failed\r\n");
    }
  }

  return APP_OK;
}
