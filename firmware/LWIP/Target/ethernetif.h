/* USER CODE BEGIN Header */
/**
 ******************************************************************************
  * File Name          : ethernetif.h
  * Description        : This file provides initialization code for LWIP
  *                      middleWare.
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
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef __ETHERNETIF_H__
#define __ETHERNETIF_H__
#include "lwip/err.h"
#include "lwip/netif.h"
#include "app_common_types.h"

/* Within 'USER CODE' section, code will be kept by default at each generation */
/* USER CODE BEGIN 0 */
/**
 * @brief Snapshot of the Ethernet low-level driver state exported to the CLI.
 */
typedef struct {
  uint8_t user_phy_ready;  /**< Set once the USER_PHY backend has been initialized. */
  uint8_t eth_started;     /**< Set once HAL ETH start has completed. */
  uint8_t link_up;         /**< Last link state observed by the low-level driver. */
  int32_t last_phy_status; /**< Last USER_PHY status code returned by link polling. */
  uint32_t mac_speed;      /**< Last MAC speed configuration applied by the driver. */
  uint32_t mac_duplex;     /**< Last MAC duplex configuration applied by the driver. */
  uint32_t sample_tick_ms; /**< System tick captured with the snapshot. */
} ethernetif_runtime_state_t;

/* USER CODE END 0 */

/* Exported functions ------------------------------------------------------- */
err_t ethernetif_init(struct netif *netif);

void ethernetif_input(struct netif *netif);
void ethernet_link_check_state(struct netif *netif);

void Error_Handler(void);
u32_t sys_jiffies(void);
u32_t sys_now(void);

/* USER CODE BEGIN 1 */
/**
 * @brief Performs a low-level Ethernet preflight check used during bring-up.
 *
 * @return @ref APP_OK when the low-level driver prerequisites look valid.
 */
app_status_t ethernetif_preflight(void);

/**
 * @brief Returns a CLI-friendly snapshot of the Ethernet low-level driver state.
 *
 * @param[out] out Destination structure for the runtime snapshot.
 *
 * @return @ref APP_OK on success.
 */
app_status_t ethernetif_get_runtime_state(ethernetif_runtime_state_t *out);

/* USER CODE END 1 */
#endif
