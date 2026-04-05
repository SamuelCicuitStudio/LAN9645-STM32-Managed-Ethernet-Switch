#include "diag/diag_core.h"

/**
 * @file diag_core.c
 * @brief In-memory diagnostics store for event counters and last-failure context.
 */

#include <string.h>

#include "time/time_service.h"

#define DIAG_RING_SIZE (32u)

/**
 * @brief One entry in the diagnostics history ring.
 */
typedef struct {
  diag_event_t evt;
  app_time_t ts;
} diag_ring_entry_t;

static uint8_t s_initialized = 0u;
static diag_error_counter_t s_errors[DIAG_EVT_COUNT];
static diag_event_t s_last_evt;
static uint8_t s_has_last_evt = 0u;

static diag_ring_entry_t s_ring[DIAG_RING_SIZE];
static uint32_t s_ring_head = 0u;
static uint32_t s_ring_count = 0u;

/**
 * @brief Validates that an event identifier is within the supported range.
 *
 * @param[in] id Event identifier to validate.
 *
 * @return 1 when valid, otherwise 0.
 */
static uint8_t diag_core_is_valid_id(diag_event_id_t id)
{
  return (id < DIAG_EVT_COUNT) ? 1u : 0u;
}

/**
 * @brief Clears all diagnostics counters, last-event state, and history.
 */
static void diag_core_reset_state(void)
{
  uint32_t i = 0u;

  memset(s_errors, 0, sizeof(s_errors));
  memset(&s_last_evt, 0, sizeof(s_last_evt));
  memset(s_ring, 0, sizeof(s_ring));

  s_has_last_evt = 0u;
  s_ring_head = 0u;
  s_ring_count = 0u;

  for (i = 0u; i < (uint32_t)DIAG_EVT_COUNT; ++i) {
    s_errors[i].id = (diag_event_id_t)i;
  }
}

/**
 * @brief Initializes the diagnostics store.
 *
 * @return @ref APP_OK on success.
 */
app_status_t diag_core_init(void)
{
  if (s_initialized != 0u) {
    return APP_OK;
  }

  diag_core_reset_state();
  s_initialized = 1u;
  return APP_OK;
}

/**
 * @brief Records a diagnostics event and updates aggregate counters.
 *
 * @param[in] evt Event to record.
 *
 * @return @ref APP_OK on success.
 */
app_status_t diag_core_report(const diag_event_t *evt)
{
  app_time_t now = {0};
  diag_error_counter_t *slot = NULL;

  if (evt == NULL) {
    return APP_EINVAL;
  }
  if (diag_core_is_valid_id(evt->id) == 0u) {
    return APP_EINVAL;
  }
  if (s_initialized == 0u) {
    (void)diag_core_init();
  }

  (void)time_service_now(&now);

  slot = &s_errors[(uint32_t)evt->id];
  if (slot->count == 0u) {
    slot->first_ts = now;
  }
  slot->count++;
  slot->last_ts = now;
  slot->last_dev = evt->dev;
  slot->last_reg = evt->reg;
  slot->last_arg0 = evt->arg0;
  slot->last_arg1 = evt->arg1;

  s_last_evt = *evt;
  s_has_last_evt = 1u;

  s_ring[s_ring_head].evt = *evt;
  s_ring[s_ring_head].ts = now;
  s_ring_head = (s_ring_head + 1u) % DIAG_RING_SIZE;
  if (s_ring_count < DIAG_RING_SIZE) {
    s_ring_count++;
  }

  return APP_OK;
}

/**
 * @brief Returns the stored counter for one event identifier.
 *
 * @param[in] id Event identifier to query.
 * @param[out] out Destination counter snapshot.
 *
 * @return @ref APP_OK on success.
 */
app_status_t diag_core_get_error(diag_event_id_t id, diag_error_counter_t *out)
{
  if (out == NULL) {
    return APP_EINVAL;
  }
  if (diag_core_is_valid_id(id) == 0u) {
    return APP_EINVAL;
  }
  if (s_initialized == 0u) {
    (void)diag_core_init();
  }

  *out = s_errors[(uint32_t)id];
  return APP_OK;
}

/**
 * @brief Returns the last event reported into the diagnostics core.
 *
 * @param[out] out Destination event snapshot.
 *
 * @return @ref APP_OK on success.
 */
app_status_t diag_core_get_last(diag_event_t *out)
{
  if (out == NULL) {
    return APP_EINVAL;
  }
  if (s_initialized == 0u) {
    (void)diag_core_init();
  }
  if (s_has_last_evt == 0u) {
    return APP_ESTATE;
  }

  *out = s_last_evt;
  return APP_OK;
}

/**
 * @brief Clears all diagnostics state while keeping the core initialized.
 *
 * @return @ref APP_OK on success.
 */
app_status_t diag_core_clear_all(void)
{
  if (s_initialized == 0u) {
    (void)diag_core_init();
  }

  diag_core_reset_state();
  return APP_OK;
}
