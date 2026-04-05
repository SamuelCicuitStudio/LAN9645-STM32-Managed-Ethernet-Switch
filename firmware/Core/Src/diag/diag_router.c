#include "diag/diag_router.h"

#include <stdio.h>
#include <string.h>

#include "main.h"
#include "time/time_service.h"
#include "usb_device.h"
#include "usbd_cdc_if.h"

extern UART_HandleTypeDef huart4;
extern USBD_HandleTypeDef hUsbDeviceFS;

static uint8_t s_initialized = 0u;
static diag_router_state_t s_state = {
  APP_OWNER_NONE,
  0u,
  0u,
  1u,
  0u
};

static const char *diag_router_evt_to_str(diag_event_id_t id)
{
  switch (id) {
    case DIAG_EVT_BOOT: return "DIAG_EVT_BOOT";
    case DIAG_EVT_LAN_SPI_IFSTAT: return "DIAG_EVT_LAN_SPI_IFSTAT";
    case DIAG_EVT_LAN_SPI_TIMEOUT: return "DIAG_EVT_LAN_SPI_TIMEOUT";
    case DIAG_EVT_LAN_STRAP_EXPECTATION: return "DIAG_EVT_LAN_STRAP_EXPECTATION";
    case DIAG_EVT_LAN_RMII_LINK_TIMEOUT: return "DIAG_EVT_LAN_RMII_LINK_TIMEOUT";
    case DIAG_EVT_LAN_SERDES0_LINK_TIMEOUT: return "DIAG_EVT_LAN_SERDES0_LINK_TIMEOUT";
    case DIAG_EVT_LAN_SERDES1_LINK_TIMEOUT: return "DIAG_EVT_LAN_SERDES1_LINK_TIMEOUT";
    case DIAG_EVT_LAN_IRQ_STUCK: return "DIAG_EVT_LAN_IRQ_STUCK";
    case DIAG_EVT_LAN_IRQ_CAUSE: return "DIAG_EVT_LAN_IRQ_CAUSE";
    case DIAG_EVT_LAN_IRQ_ACK_FAIL: return "DIAG_EVT_LAN_IRQ_ACK_FAIL";
    case DIAG_EVT_LAN_DROP_COUNTER_ALERT: return "DIAG_EVT_LAN_DROP_COUNTER_ALERT";
    default: return "DIAG_EVT_UNKNOWN";
  }
}

static const char *diag_router_sev_to_str(diag_severity_t sev)
{
  switch (sev) {
    case DIAG_SEV_INFO: return "INFO";
    case DIAG_SEV_WARN: return "WARN";
    case DIAG_SEV_ERR: return "ERR";
    case DIAG_SEV_FATAL: return "FATAL";
    default: return "UNK";
  }
}

static app_status_t diag_router_tx_usb(const char *line, uint16_t len)
{
  uint8_t usb_st = 0u;

  if ((line == NULL) || (len == 0u)) {
    return APP_EINVAL;
  }
  if (s_state.usb_ready == 0u) {
    return APP_ESTATE;
  }

  usb_st = CDC_Transmit_FS((uint8_t *)line, len);
  if (usb_st == USBD_OK) {
    return APP_OK;
  }

  return APP_EIO;
}

static app_status_t diag_router_tx_uart(const char *line, uint16_t len)
{
  if ((line == NULL) || (len == 0u)) {
    return APP_EINVAL;
  }
  if (s_state.uart_ready == 0u) {
    return APP_ESTATE;
  }

  if (HAL_UART_Transmit(&huart4, (uint8_t *)line, len, 20u) != HAL_OK) {
    return APP_EIO;
  }

  return APP_OK;
}

static app_status_t diag_router_tx_by_owner(const char *line, uint16_t len)
{
  app_status_t st = APP_ESTATE;
  app_status_t st_usb = APP_ESTATE;
  app_status_t st_uart = APP_ESTATE;

  if ((line == NULL) || (len == 0u)) {
    return APP_EINVAL;
  }

  if (s_state.owner == APP_OWNER_USB) {
    return diag_router_tx_usb(line, len);
  }
  if (s_state.owner == APP_OWNER_UART4) {
    return diag_router_tx_uart(line, len);
  }

  if (s_state.mirror_when_none == 0u) {
    return APP_ESTATE;
  }

  if (s_state.usb_ready != 0u) {
    st_usb = diag_router_tx_usb(line, len);
  }
  if (s_state.uart_ready != 0u) {
    st_uart = diag_router_tx_uart(line, len);
  }

  if ((st_usb == APP_OK) || (st_uart == APP_OK)) {
    st = APP_OK;
  } else if ((st_usb == APP_EIO) || (st_uart == APP_EIO)) {
    st = APP_EIO;
  } else {
    st = APP_ESTATE;
  }

  return st;
}

app_status_t diag_router_init(void)
{
  if (s_initialized != 0u) {
    return APP_OK;
  }

  s_state.owner = APP_OWNER_NONE;
  s_state.usb_ready = 0u;
  s_state.uart_ready = 0u;
  s_state.mirror_when_none = 1u;
  s_state.bridge_usb_uart_on = 0u;
  s_initialized = 1u;

  return APP_OK;
}

app_status_t diag_router_set_owner(app_owner_t owner)
{
  if ((owner != APP_OWNER_NONE) && (owner != APP_OWNER_USB) && (owner != APP_OWNER_UART4)) {
    return APP_EINVAL;
  }
  if (s_initialized == 0u) {
    (void)diag_router_init();
  }

  s_state.owner = owner;
  return APP_OK;
}

app_owner_t diag_router_get_owner(void)
{
  if (s_initialized == 0u) {
    (void)diag_router_init();
  }
  return s_state.owner;
}

app_status_t diag_router_set_usb_ready(uint8_t ready)
{
  if (s_initialized == 0u) {
    (void)diag_router_init();
  }
  s_state.usb_ready = (ready != 0u) ? 1u : 0u;
  return APP_OK;
}

app_status_t diag_router_set_uart_ready(uint8_t ready)
{
  if (s_initialized == 0u) {
    (void)diag_router_init();
  }
  s_state.uart_ready = (ready != 0u) ? 1u : 0u;
  return APP_OK;
}

app_status_t diag_router_write_text(const char *line)
{
  size_t line_len = 0u;

  if (line == NULL) {
    return APP_EINVAL;
  }
  if (s_initialized == 0u) {
    (void)diag_router_init();
  }

  line_len = strlen(line);
  if (line_len == 0u) {
    return APP_OK;
  }
  if (line_len > 0xFFFFu) {
    return APP_EINVAL;
  }

  return diag_router_tx_by_owner(line, (uint16_t)line_len);
}

app_status_t diag_router_write_event(const diag_event_t *evt)
{
  app_time_t now = {0};
  char ts[40];
  char line[220];
  const char *mod = "LAN";
  int n = 0;

  if (evt == NULL) {
    return APP_EINVAL;
  }
  if (s_initialized == 0u) {
    (void)diag_router_init();
  }

  if (evt->id == DIAG_EVT_BOOT) {
    mod = "SYS";
  }

  (void)time_service_now(&now);
  if (time_service_format(&now, ts, (uint32_t)sizeof(ts)) != APP_OK) {
    (void)snprintf(ts, sizeof(ts), "T+%lums", (unsigned long)HAL_GetTick());
  }

  n = snprintf(
    line,
    sizeof(line),
    "[%s][%s][%s][%s] dev=%lu reg=0x%08lX a0=%lu a1=%lu\r\n",
    ts,
    diag_router_sev_to_str(evt->severity),
    mod,
    diag_router_evt_to_str(evt->id),
    (unsigned long)evt->dev,
    (unsigned long)evt->reg,
    (unsigned long)evt->arg0,
    (unsigned long)evt->arg1
  );
  if ((n < 0) || ((size_t)n >= sizeof(line))) {
    return APP_EIO;
  }

  return diag_router_write_text(line);
}

app_status_t diag_router_set_bridge(uint8_t on)
{
  if (s_initialized == 0u) {
    (void)diag_router_init();
  }
  s_state.bridge_usb_uart_on = (on != 0u) ? 1u : 0u;
  return APP_OK;
}

app_status_t diag_router_get_state(diag_router_state_t *out)
{
  if (out == NULL) {
    return APP_EINVAL;
  }
  if (s_initialized == 0u) {
    (void)diag_router_init();
  }

  *out = s_state;
  return APP_OK;
}

app_status_t diag_router_poll(void)
{
  if (s_initialized == 0u) {
    (void)diag_router_init();
  }

  if (hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED) {
    s_state.usb_ready = 1u;
  } else {
    s_state.usb_ready = 0u;
    if (s_state.owner == APP_OWNER_USB) {
      s_state.owner = APP_OWNER_NONE;
    }
  }

  if (s_state.uart_ready == 0u) {
    if (s_state.owner == APP_OWNER_UART4) {
      s_state.owner = APP_OWNER_NONE;
    }
  }

  return APP_OK;
}
