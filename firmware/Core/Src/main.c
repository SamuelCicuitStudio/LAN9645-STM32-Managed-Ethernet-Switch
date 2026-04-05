/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "lwip.h"
#include "usb_device.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "ethernetif.h"
#include "diag/diag_core.h"
#include "diag/diag_router.h"
#include "app/app_orchestrator.h"
#include "cli/cli_core.h"
#include "cli/cli_ingress.h"
#include "lan9645/lan9645_core.h"
#include "lan9645/lan9645_feature_matrix.h"
#include "lan9645/lan9645_profile_managed_rmii1.h"
#include "lan9645/lan9645_reg.h"
#include "lan9645/lan9645_runtime.h"
#include "lan9645/lan9645_spi.h"
#include "lan9645/lan9645_target_map.h"
#include "time/time_service.h"
#include "ui/ui_service.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define LAN9645_SPI_CLOCK_FALLBACK_HZ (24000000u)
#define LAN9645_IRQ_STUCK_TIMEOUT_MS (200u)
#define LAN9645_IRQ_CAUSE_REPORT_INTERVAL_MS (250u)

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

CRC_HandleTypeDef hcrc;

RNG_HandleTypeDef hrng;

RTC_HandleTypeDef hrtc;

SPI_HandleTypeDef hspi1;

UART_HandleTypeDef huart4;

/* USER CODE BEGIN PV */
static lan9645_spi_t g_lan_spi;
static lan9645_reg_t g_lan_reg;
static lan9645_core_t g_lan_core;
static lan9645_feature_matrix_t g_lan_features;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI1_Init(void);
static void MX_UART4_Init(void);
static void MX_RTC_Init(void);
static void MX_CRC_Init(void);
static void MX_RNG_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/**
 * @brief Converts STM32 SPI prescaler enum to numeric divider value.
 */
static uint32_t lan9645_spi_prescaler_to_div(uint32_t prescaler)
{
  switch (prescaler)
  {
    case SPI_BAUDRATEPRESCALER_2: return 2u;
    case SPI_BAUDRATEPRESCALER_4: return 4u;
    case SPI_BAUDRATEPRESCALER_8: return 8u;
    case SPI_BAUDRATEPRESCALER_16: return 16u;
    case SPI_BAUDRATEPRESCALER_32: return 32u;
    case SPI_BAUDRATEPRESCALER_64: return 64u;
    case SPI_BAUDRATEPRESCALER_128: return 128u;
    case SPI_BAUDRATEPRESCALER_256: return 256u;
    default: return 0u;
  }
}

/**
 * @brief Estimates effective SPI1 serial clock used by LAN9645 transactions.
 *
 * The clock is derived from the SPI1 kernel source selected in RCC plus the
 * configured prescaler in `hspi->Init.BaudRatePrescaler`.
 */
static uint32_t lan9645_spi_effective_clock_hz(const SPI_HandleTypeDef *hspi)
{
  uint32_t spi_ker_hz = 0u;
  uint32_t div = 0u;

  if (hspi == NULL)
  {
    return 0u;
  }

  spi_ker_hz = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_SPI1);
  div = lan9645_spi_prescaler_to_div(hspi->Init.BaudRatePrescaler);
  if ((spi_ker_hz == 0u) || (div == 0u))
  {
    return 0u;
  }

  return (spi_ker_hz / div);
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MPU Configuration--------------------------------------------------------*/
  MPU_Config();

  /* Enable the CPU Cache */

  /* Enable I-Cache---------------------------------------------------------*/
  SCB_EnableICache();

  /* Enable D-Cache---------------------------------------------------------*/
  SCB_EnableDCache();

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_SPI1_Init();
  MX_UART4_Init();
  MX_RTC_Init();
  MX_CRC_Init();
  MX_RNG_Init();
  MX_USB_DEVICE_Init();
  /* USER CODE BEGIN 2 */
  /* Resolve the SPI bus clock used by LAN9645 setup and read padding rules. */
  uint32_t lan_spi_clock_hz = lan9645_spi_effective_clock_hz(&hspi1);
  if (lan_spi_clock_hz == 0u)
  {
    /* Keep deterministic setup even when the peripheral clock query fails. */
    lan_spi_clock_hz = LAN9645_SPI_CLOCK_FALLBACK_HZ;
  }

  /* Initialize timing and diagnostics first so all bring-up failures are visible. */
  if (time_service_init() != APP_OK)
  {
    Error_Handler();
  }
  if (diag_core_init() != APP_OK)
  {
    Error_Handler();
  }
  if (diag_router_init() != APP_OK)
  {
    Error_Handler();
  }
  /* USB CDC is the primary CLI/diag channel, UART4 remains as fallback/mirror. */
  if (diag_router_set_owner(APP_OWNER_USB) != APP_OK)
  {
    Error_Handler();
  }
  if (diag_router_set_uart_ready(1u) != APP_OK)
  {
    Error_Handler();
  }
  /* Bring up app state machine and UI/CLI services before LAN setup. */
  if (app_orchestrator_init() != APP_OK)
  {
    Error_Handler();
  }
  (void)app_orchestrator_set_state(APP_STATE_BOOT);
  if (ui_service_init() != APP_OK)
  {
    Error_Handler();
  }
  if (cli_core_init() != APP_OK)
  {
    Error_Handler();
  }
  if (cli_ingress_init() != APP_OK)
  {
    Error_Handler();
  }
  (void)ui_service_set_status(0u);
  (void)ui_service_set_error(0u);
  (void)ui_service_set_run(0u);

  /* Build the LAN9645 SPI/register/core stack from low-level to high-level APIs. */
  if (lan9645_spi_init(&g_lan_spi, &hspi1, SPI_NSS_GPIO_Port, SPI_NSS_Pin) != APP_OK)
  {
    Error_Handler();
  }
  if (lan9645_reg_init(&g_lan_reg, &g_lan_spi) != APP_OK)
  {
    Error_Handler();
  }
  if (lan9645_target_map_apply_board_defaults(&g_lan_reg) != APP_OK)
  {
    Error_Handler();
  }
  if (lan9645_core_init(&g_lan_core, &g_lan_reg, LAN_RST_GPIO_Port, LAN_RST_Pin) != APP_OK)
  {
    Error_Handler();
  }
  lan9645_runtime_set_core(&g_lan_core);
  if (lan9645_feature_matrix_init(&g_lan_features, &g_lan_core) != APP_OK)
  {
    Error_Handler();
  }
  lan9645_runtime_set_feature_matrix(&g_lan_features);
  if (app_orchestrator_lan_irq_init(
      LAN9645_IRQ_STUCK_TIMEOUT_MS,
      LAN9645_IRQ_CAUSE_REPORT_INTERVAL_MS) != APP_OK)
  {
    Error_Handler();
  }
  (void)app_orchestrator_set_state(APP_STATE_LAN_RESET);
  if (lan9645_core_hw_reset(&g_lan_core, 2u, 20u) != APP_OK)
  {
    Error_Handler();
  }
  if (lan9645_core_spi_setup(&g_lan_core, lan_spi_clock_hz) != APP_OK)
  {
    Error_Handler();
  }

  /* Apply board profile then enable firmware-selected feature policies. */
  (void)app_orchestrator_set_state(APP_STATE_LAN_SPI_TEST);
  {
    lan9645_profile_result_t lan_profile = {0};
    (void)app_orchestrator_set_state(APP_STATE_LAN_PROFILE_APPLY);
    app_status_t lan_st = lan9645_profile_managed_rmii1_apply(&g_lan_core, &lan_profile);
    if (lan_st != APP_OK)
    {
      diag_event_t lan_evt;
      lan_evt.id = DIAG_EVT_LAN_SPI_TIMEOUT;
      lan_evt.severity = DIAG_SEV_ERR;
      lan_evt.dev = 0u;
      lan_evt.reg = 0u;
      lan_evt.arg0 = (uint32_t)lan_st;
      lan_evt.arg1 = 0u;
      (void)diag_core_report(&lan_evt);
      (void)diag_router_write_event(&lan_evt);
      (void)app_orchestrator_set_fault((uint32_t)lan_st);
      (void)ui_service_set_error(1u);
      Error_Handler();
    }
    else
    {
      diag_event_t lan_evt;
      lan_evt.id = DIAG_EVT_LAN_SPI_IFSTAT;
      lan_evt.severity = DIAG_SEV_INFO;
      lan_evt.dev = 0u;
      lan_evt.reg = 0u;
      lan_evt.arg0 = lan_profile.feat_disable;
      lan_evt.arg1 = lan_profile.chip_id;
      (void)diag_core_report(&lan_evt);
      (void)diag_router_write_event(&lan_evt);
      {
        app_status_t feat_st = lan9645_feature_matrix_apply_all(&g_lan_features);
        if (feat_st != APP_OK)
        {
          diag_event_t feat_evt;
          feat_evt.id = DIAG_EVT_LAN_FEAT_CFG_FAIL;
          feat_evt.severity = DIAG_SEV_WARN;
          feat_evt.dev = 0u;
          feat_evt.reg = 0u;
          feat_evt.arg0 = (uint32_t)(int32_t)feat_st;
          feat_evt.arg1 = 0u;
          (void)diag_core_report(&feat_evt);
          (void)diag_router_write_event(&feat_evt);
        }
      }
      MX_LWIP_Init();
      {
        /* Ensure ethernetif + custom PHY glue is ready before entering RUN state. */
        app_status_t eth_pf = ethernetif_preflight();
        if (eth_pf != APP_OK)
        {
          diag_event_t eth_evt;
          eth_evt.id = DIAG_EVT_LAN_SPI_TIMEOUT;
          eth_evt.severity = DIAG_SEV_ERR;
          eth_evt.dev = 8u;
          eth_evt.reg = 0u;
          eth_evt.arg0 = (uint32_t)eth_pf;
          eth_evt.arg1 = 1u;
          (void)diag_core_report(&eth_evt);
          (void)diag_router_write_event(&eth_evt);
          (void)app_orchestrator_set_fault((uint32_t)eth_pf);
          (void)ui_service_set_error(1u);
          Error_Handler();
        }
      }
      (void)app_orchestrator_set_state(APP_STATE_NET_READY);
      (void)ui_service_set_status(1u);
      (void)ui_service_set_error(0u);
      (void)ui_service_set_run(1u);
    }
  }

  /* Publish a boot marker only after diagnostics, LAN, and network are online. */
  (void)diag_router_poll();
  {
    diag_event_t boot_evt;
    boot_evt.id = DIAG_EVT_BOOT;
    boot_evt.severity = DIAG_SEV_INFO;
    boot_evt.dev = 0u;
    boot_evt.reg = 0u;
    boot_evt.arg0 = 0u;
    boot_evt.arg1 = 0u;
    (void)diag_core_report(&boot_evt);
    (void)diag_router_write_event(&boot_evt);
  }
  (void)app_orchestrator_set_state(APP_STATE_RUN);
  (void)cli_core_exec_line("status all");

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    /* Process RX/TX packets and service outgoing diagnostics stream. */
    MX_LWIP_Process();
    (void)diag_router_poll();
    /* Supervise LAN IRQ causes/ack flow and expose errors through diagnostics. */
    (void)app_orchestrator_lan_irq_step(
      &g_lan_core,
      (HAL_GPIO_ReadPin(IRQ0_GPIO_Port, IRQ0_Pin) == GPIO_PIN_SET) ? 1u : 0u,
      HAL_GetTick()
    );

    /* Execute CLI commands and ingest fallback UART4 bytes into CLI ingress. */
    (void)cli_core_poll();
    {
      uint8_t rx = 0u;
      while (HAL_UART_Receive(&huart4, &rx, 1u, 0u) == HAL_OK)
      {
        (void)cli_ingress_feed_byte(rx, APP_CH_UART4);
      }
    }
    (void)cli_ingress_poll();

    /* Refresh LED/button service and map user button press to quick status dump. */
    (void)ui_service_poll();
    if (ui_service_take_button_event() != 0u)
    {
      (void)cli_core_exec_line("status all");
    }
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI48|RCC_OSCILLATORTYPE_LSI
                              |RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 24;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_3;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV1;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief CRC Initialization Function
  * @param None
  * @retval None
  */
static void MX_CRC_Init(void)
{

  /* USER CODE BEGIN CRC_Init 0 */

  /* USER CODE END CRC_Init 0 */

  /* USER CODE BEGIN CRC_Init 1 */

  /* USER CODE END CRC_Init 1 */
  hcrc.Instance = CRC;
  hcrc.Init.DefaultPolynomialUse = DEFAULT_POLYNOMIAL_ENABLE;
  hcrc.Init.DefaultInitValueUse = DEFAULT_INIT_VALUE_ENABLE;
  hcrc.Init.InputDataInversionMode = CRC_INPUTDATA_INVERSION_NONE;
  hcrc.Init.OutputDataInversionMode = CRC_OUTPUTDATA_INVERSION_DISABLE;
  hcrc.InputDataFormat = CRC_INPUTDATA_FORMAT_BYTES;
  if (HAL_CRC_Init(&hcrc) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN CRC_Init 2 */

  /* USER CODE END CRC_Init 2 */

}

/**
  * @brief RNG Initialization Function
  * @param None
  * @retval None
  */
static void MX_RNG_Init(void)
{

  /* USER CODE BEGIN RNG_Init 0 */

  /* USER CODE END RNG_Init 0 */

  /* USER CODE BEGIN RNG_Init 1 */

  /* USER CODE END RNG_Init 1 */
  hrng.Instance = RNG;
  hrng.Init.ClockErrorDetection = RNG_CED_ENABLE;
  if (HAL_RNG_Init(&hrng) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN RNG_Init 2 */

  /* USER CODE END RNG_Init 2 */

}

/**
  * @brief RTC Initialization Function
  * @param None
  * @retval None
  */
static void MX_RTC_Init(void)
{

  /* USER CODE BEGIN RTC_Init 0 */

  /* USER CODE END RTC_Init 0 */

  /* USER CODE BEGIN RTC_Init 1 */

  /* USER CODE END RTC_Init 1 */

  /** Initialize RTC Only
  */
  hrtc.Instance = RTC;
  hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
  hrtc.Init.AsynchPrediv = 127;
  hrtc.Init.SynchPrediv = 255;
  hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
  hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
  hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
  hrtc.Init.OutPutRemap = RTC_OUTPUT_REMAP_NONE;
  if (HAL_RTC_Init(&hrtc) != HAL_OK)
  {
    Error_Handler();
  }

  /** Enable Calibrartion
  */
  if (HAL_RTCEx_SetCalibrationOutPut(&hrtc, RTC_CALIBOUTPUT_1HZ) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN RTC_Init 2 */

  /* USER CODE END RTC_Init 2 */

}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 0x0;
  hspi1.Init.NSSPMode = SPI_NSS_PULSE_ENABLE;
  hspi1.Init.NSSPolarity = SPI_NSS_POLARITY_LOW;
  hspi1.Init.FifoThreshold = SPI_FIFO_THRESHOLD_01DATA;
  hspi1.Init.TxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
  hspi1.Init.RxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
  hspi1.Init.MasterSSIdleness = SPI_MASTER_SS_IDLENESS_00CYCLE;
  hspi1.Init.MasterInterDataIdleness = SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
  hspi1.Init.MasterReceiverAutoSusp = SPI_MASTER_RX_AUTOSUSP_DISABLE;
  hspi1.Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_DISABLE;
  hspi1.Init.IOSwap = SPI_IO_SWAP_DISABLE;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief UART4 Initialization Function
  * @param None
  * @retval None
  */
static void MX_UART4_Init(void)
{

  /* USER CODE BEGIN UART4_Init 0 */

  /* USER CODE END UART4_Init 0 */

  /* USER CODE BEGIN UART4_Init 1 */

  /* USER CODE END UART4_Init 1 */
  huart4.Instance = UART4;
  huart4.Init.BaudRate = 115200;
  huart4.Init.WordLength = UART_WORDLENGTH_8B;
  huart4.Init.StopBits = UART_STOPBITS_1;
  huart4.Init.Parity = UART_PARITY_NONE;
  huart4.Init.Mode = UART_MODE_TX_RX;
  huart4.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart4.Init.OverSampling = UART_OVERSAMPLING_16;
  huart4.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart4.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart4.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart4) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&huart4, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart4, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&huart4) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN UART4_Init 2 */

  /* USER CODE END UART4_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */
  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOI_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(SPI_NSS_GPIO_Port, SPI_NSS_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOG, HEART_Pin|STATUS_Pin|ERR_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LAN_RST_GPIO_Port, LAN_RST_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : USR_Pin */
  GPIO_InitStruct.Pin = USR_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(USR_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : SPI_NSS_Pin */
  GPIO_InitStruct.Pin = SPI_NSS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(SPI_NSS_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : HEART_Pin STATUS_Pin ERR_Pin */
  GPIO_InitStruct.Pin = HEART_Pin|STATUS_Pin|ERR_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

  /*Configure GPIO pin : LAN_RST_Pin */
  GPIO_InitStruct.Pin = LAN_RST_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LAN_RST_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : IRQ0_Pin */
  GPIO_InitStruct.Pin = IRQ0_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(IRQ0_GPIO_Port, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(IRQ0_EXTI_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(IRQ0_EXTI_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */
  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == IRQ0_Pin)
  {
    /* Keep EXTI ISR minimal: only latch edge count, full handling runs in main loop. */
    lan9645_runtime_irq_on_exti();
  }
}

/* USER CODE END 4 */

 /* MPU Configuration */

void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  /* Disables the MPU */
  HAL_MPU_Disable();

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0x0;
  MPU_InitStruct.Size = MPU_REGION_SIZE_4GB;
  MPU_InitStruct.SubRegionDisable = 0x87;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /* Keep ETH DMA descriptors and Rx buffers in a non-cacheable RAM_D2 region. */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER1;
  MPU_InitStruct.BaseAddress = 0x30000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_256KB;
  MPU_InitStruct.SubRegionDisable = 0x00;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL1;
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /* Enables the MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
