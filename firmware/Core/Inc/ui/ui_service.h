#ifndef UI_SERVICE_H
#define UI_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "app_common_types.h"

app_status_t ui_service_init(void);
app_status_t ui_service_poll(void);
app_status_t ui_service_set_status(uint8_t on);
app_status_t ui_service_set_error(uint8_t on);
app_status_t ui_service_set_run(uint8_t on);
uint8_t ui_service_take_button_event(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_SERVICE_H */
