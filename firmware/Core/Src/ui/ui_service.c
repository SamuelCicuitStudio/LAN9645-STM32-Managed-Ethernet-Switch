#include "ui/ui_service.h"

#include "main.h"

#define UI_HEARTBEAT_PERIOD_MS      (500u)
#define UI_BUTTON_DEBOUNCE_MS       (30u)

typedef struct {
  uint8_t initialized;
  uint8_t status_on;
  uint8_t error_on;
  uint8_t run_on;
  uint8_t heartbeat_level;
  uint32_t next_heartbeat_ms;
  uint8_t btn_last_raw;
  uint8_t btn_stable;
  uint32_t btn_change_ms;
  uint8_t btn_press_event;
} ui_state_t;

static ui_state_t s_ui = {0};

static uint8_t ui_read_button_raw(void)
{
  GPIO_PinState pin = HAL_GPIO_ReadPin(USR_GPIO_Port, USR_Pin);
  return (pin == GPIO_PIN_RESET) ? 1u : 0u;
}

static void ui_apply_outputs(void)
{
  HAL_GPIO_WritePin(HEART_GPIO_Port, HEART_Pin, (s_ui.heartbeat_level != 0u) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(STATUS_GPIO_Port, STATUS_Pin, (s_ui.status_on != 0u) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(ERR_GPIO_Port, ERR_Pin, (s_ui.error_on != 0u) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

app_status_t ui_service_init(void)
{
  s_ui.initialized = 1u;
  s_ui.status_on = 0u;
  s_ui.error_on = 0u;
  s_ui.run_on = 0u;
  s_ui.heartbeat_level = 0u;
  s_ui.next_heartbeat_ms = HAL_GetTick() + UI_HEARTBEAT_PERIOD_MS;
  s_ui.btn_last_raw = ui_read_button_raw();
  s_ui.btn_stable = s_ui.btn_last_raw;
  s_ui.btn_change_ms = HAL_GetTick();
  s_ui.btn_press_event = 0u;
  ui_apply_outputs();
  return APP_OK;
}

app_status_t ui_service_set_status(uint8_t on)
{
  if (s_ui.initialized == 0u) {
    (void)ui_service_init();
  }

  s_ui.status_on = (on != 0u) ? 1u : 0u;
  ui_apply_outputs();
  return APP_OK;
}

app_status_t ui_service_set_error(uint8_t on)
{
  if (s_ui.initialized == 0u) {
    (void)ui_service_init();
  }

  s_ui.error_on = (on != 0u) ? 1u : 0u;
  ui_apply_outputs();
  return APP_OK;
}

app_status_t ui_service_set_run(uint8_t on)
{
  if (s_ui.initialized == 0u) {
    (void)ui_service_init();
  }

  s_ui.run_on = (on != 0u) ? 1u : 0u;
  if (s_ui.run_on == 0u) {
    s_ui.heartbeat_level = 0u;
    ui_apply_outputs();
  } else {
    s_ui.next_heartbeat_ms = HAL_GetTick() + UI_HEARTBEAT_PERIOD_MS;
  }
  return APP_OK;
}

uint8_t ui_service_take_button_event(void)
{
  uint8_t evt = s_ui.btn_press_event;
  s_ui.btn_press_event = 0u;
  return evt;
}

app_status_t ui_service_poll(void)
{
  uint32_t now = HAL_GetTick();
  uint8_t btn_raw = 0u;

  if (s_ui.initialized == 0u) {
    (void)ui_service_init();
  }

  if ((s_ui.run_on != 0u) && ((int32_t)(now - s_ui.next_heartbeat_ms) >= 0)) {
    s_ui.heartbeat_level ^= 1u;
    s_ui.next_heartbeat_ms = now + UI_HEARTBEAT_PERIOD_MS;
    ui_apply_outputs();
  }

  btn_raw = ui_read_button_raw();
  if (btn_raw != s_ui.btn_last_raw) {
    s_ui.btn_last_raw = btn_raw;
    s_ui.btn_change_ms = now;
  }

  if ((uint32_t)(now - s_ui.btn_change_ms) >= UI_BUTTON_DEBOUNCE_MS) {
    if (s_ui.btn_stable != s_ui.btn_last_raw) {
      uint8_t prev = s_ui.btn_stable;
      s_ui.btn_stable = s_ui.btn_last_raw;
      if ((prev == 0u) && (s_ui.btn_stable == 1u)) {
        s_ui.btn_press_event = 1u;
      }
    }
  }

  return APP_OK;
}
