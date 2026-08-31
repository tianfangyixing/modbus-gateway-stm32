/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
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
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>

#include "altcp_tls_mbedtls_mem.h"
#include "debug_log.h"
#include "configuration_service.h"
#include "external_flash.h"
#include "lwip/apps/lwiperf.h"
#include "lwip/dhcp.h"
#include "lwip/dns.h"
#include "lwip/ip_addr.h"
#include "lwip/netif.h"
#include "lwip/tcpip.h"
#include "lwip_random.h"
#include "mbedtls/entropy.h"
#include "mbedtls/entropy_poll.h"
#include "management_transport.h"
#include "modbus_gateway_app.h"
#include "mqtt_publisher.h"
#include "sntp_service.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
typedef StaticTask_t osStaticThreadDef_t;
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

static management_transport_result_t management_initialization_result = MANAGEMENT_TRANSPORT_NOT_INITIALIZED;
static volatile bool management_configuration_ready;

/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
uint32_t defaultTaskBuffer[ 1024 ];
osStaticThreadDef_t defaultTaskControlBlock;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .cb_mem = &defaultTaskControlBlock,
  .cb_size = sizeof(defaultTaskControlBlock),
  .stack_mem = &defaultTaskBuffer[0],
  .stack_size = sizeof(defaultTaskBuffer),
  .priority = (osPriority_t) osPriorityNormal,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
void lwip_echo_server(uint16_t port);

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);

extern void MX_LWIP_Init(void);
extern void MX_USB_DEVICE_Init(void);
void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  management_initialization_result = management_transport_init();

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* init code for USB_DEVICE */
  MX_USB_DEVICE_Init();
  /* USER CODE BEGIN StartDefaultTask */

  external_flash_result_t flash_result = external_flash_init();
  if (flash_result == EXTERNAL_FLASH_RESULT_OK)
  {
    debug_log_printf("[boot] external flash ready\r\n");
  }
  else
  {
    debug_log_printf("[flash-test] FAIL: initialization, result=%d\r\n", (int)flash_result);
    debug_log_printf("[boot] external flash initialization failedp\r\n");
    while(1);
  }

  lwip_random_initialize();
  /* 先初始化lwip堆，才能校验configuration的证书 */
  MX_LWIP_Init();
  altcp_mbedtls_mem_init();

  configuration_service_result_t configuration_result = configuration_service_init();

  if (configuration_result == CONFIGURATION_SERVICE_OK)
  {
    management_configuration_ready = true;
    debug_log_printf("[boot] Configuration Service ready\r\n");
  }
  else
  {
     debug_log_printf("[boot] Configuration Service initialization failed, result=%d\r\n",
                       (int)configuration_result);
     while(1);
  }

  const configuration_network_t *network_configuration = &configuration_service_active()->network;

  if (network_configuration->mode == CONFIGURATION_NETWORK_MODE_STATIC)
  {
    ip_addr_t dns_primary;
    ip_addr_t dns_secondary;

    ip_addr_copy_from_ip4(dns_primary, network_configuration->dns_primary);
    ip_addr_copy_from_ip4(dns_secondary, network_configuration->dns_secondary);

    LOCK_TCPIP_CORE();
    dhcp_release_and_stop(netif_default);
    netif_set_addr(netif_default, &network_configuration->ip_address,
                   &network_configuration->subnet_mask, &network_configuration->gateway);
    dns_setserver(0U, &dns_primary);
    dns_setserver(1U, &dns_secondary);
    UNLOCK_TCPIP_CORE();
  }

  debug_log_printf("[boot] default task started\r\n");
  if (management_initialization_result == MANAGEMENT_TRANSPORT_OK)
  {
    debug_log_printf("[boot] Management task ready\r\n");
  }
  else
  {
    debug_log_printf("[boot] Management task initialization failed, result=%d\r\n",
                     (int)management_initialization_result);
  }
  debug_log_printf("[boot] initializing external flash\r\n");

  /* The altcp TLS path does not call mbedtls_net_init(), so initialize LwIP explicitly. */
  debug_log_printf("[boot] initializing LwIP\r\n");

  debug_log_printf("[boot] LwIP ready\r\n");
  /* Configuration validation can use mbedTLS before MQTT creates its first ALTCP TLS configuration. */

  sntp_service_init();
  debug_log_printf("[boot] SNTP service ready\r\n");

  mqtt_publisher_init();

  modbus_gateway_app_init();
  debug_log_printf("[boot] Modbus gateway ready\r\n");

  if (management_initialization_result == MANAGEMENT_TRANSPORT_OK)
  {
    management_transport_result_t management_activation_result = management_transport_activate();

    if (management_activation_result == MANAGEMENT_TRANSPORT_OK)
    {
      debug_log_printf("[boot] Management transport active\r\n");
    }
    else
    {
      debug_log_printf("[boot] Management transport activation failed, result=%d\r\n",
                       (int)management_activation_result);
    }
  }

  while (1)
  {

    vTaskDelay(pdMS_TO_TICKS(1000U));
  }

  /* USER CODE END StartDefaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

bool management_transport_configuration_is_ready(void)
{
  return management_configuration_ready;
}

/* USER CODE END Application */

