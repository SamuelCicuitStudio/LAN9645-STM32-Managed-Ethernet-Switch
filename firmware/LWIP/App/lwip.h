/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : LWIP.h
  * Description        : This file provides code for the configuration
  *                      of the LWIP.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  *************************************************************************

  */
/* USER CODE END Header */
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __mx_lwip_H
#define __mx_lwip_H
#ifdef __cplusplus
 extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "lwip/opt.h"
#include "lwip/mem.h"
#include "lwip/memp.h"
#include "netif/etharp.h"
#include "lwip/dhcp.h"
#include "lwip/netif.h"
#include "lwip/timeouts.h"
#include "ethernetif.h"
#include "app_common_types.h"

/* Includes for RTOS ---------------------------------------------------------*/
#if WITH_RTOS
#include "lwip/tcpip.h"
#endif /* WITH_RTOS */

/* USER CODE BEGIN 0 */
/**
 * @brief Snapshot of the active LwIP netif state exported to the CLI.
 */
typedef struct {
  uint8_t netif_up;        /**< Set when the LwIP netif is administratively up. */
  uint8_t link_up;         /**< Set when the LwIP netif reports a physical link. */
  uint8_t is_default;      /**< Set when this netif is the current default route. */
  uint16_t mtu;            /**< Current netif MTU. */
  uint16_t flags;          /**< Raw LwIP netif flags field. */
  uint8_t mac[6];          /**< MAC address currently assigned to the netif. */
  char ip[16];             /**< Printable IPv4 address string. */
  char netmask[16];        /**< Printable IPv4 netmask string. */
  char gateway[16];        /**< Printable IPv4 gateway string. */
  uint32_t sample_tick_ms; /**< System tick captured with the snapshot. */
} lwip_runtime_state_t;

/* USER CODE END 0 */

/* Global Variables ----------------------------------------------------------*/
extern ETH_HandleTypeDef heth;

/* LWIP init function */
void MX_LWIP_Init(void);

#if !WITH_RTOS
/* USER CODE BEGIN 1 */
/* Function defined in lwip.c to:
 *   - Read a received packet from the Ethernet buffers
 *   - Send it to the lwIP stack for handling
 *   - Handle timeouts if NO_SYS_NO_TIMERS not set
 */
void MX_LWIP_Process(void);

/**
 * @brief Returns a CLI-friendly snapshot of the LwIP runtime state.
 *
 * @param[out] out Destination structure for the runtime snapshot.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lwip_get_runtime_state(lwip_runtime_state_t *out);

/* USER CODE END 1 */
#endif /* WITH_RTOS */

#ifdef __cplusplus
}
#endif
#endif /*__ mx_lwip_H */

/**
  * @}
  */

/**
  * @}
  */
