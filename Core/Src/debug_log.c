#include "debug_log.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "usb_device.h"
#include "usbd_cdc_if.h"

#define DEBUG_LOG_QUEUE_LENGTH 4U
#define DEBUG_LOG_MESSAGE_SIZE 256U
#define DEBUG_LOG_MAX_LENGTH (DEBUG_LOG_MESSAGE_SIZE - 1U)
#define DEBUG_LOG_TX_STACK_WORDS 256U
#define DEBUG_LOG_TRUNCATION_SUFFIX "...[truncated]"
#define DEBUG_LOG_TRUNCATION_LENGTH (sizeof(DEBUG_LOG_TRUNCATION_SUFFIX) - 1U)

typedef struct
{
	uint16_t length;
	uint8_t text[DEBUG_LOG_MESSAGE_SIZE];
} DebugLogMessage;

extern USBD_HandleTypeDef hUsbDeviceHS;

static uint8_t debugLogQueueStorage[DEBUG_LOG_QUEUE_LENGTH * sizeof(DebugLogMessage)];
static StaticQueue_t debugLogQueueControlBlock;
static QueueHandle_t debugLogQueue;

static StackType_t debugLogTxStack[DEBUG_LOG_TX_STACK_WORDS];
static StaticTask_t debugLogTxControlBlock;
static volatile BaseType_t debugLogInitialized = pdFALSE;

static void debug_log_tx(void *argument);
static uint8_t debug_log_usb_transmit(DebugLogMessage *message);
static bool debug_log_usb_tx_pending(void);

void debug_log_init(void)
{
	TaskHandle_t txHandle;

	if (debugLogInitialized != pdFALSE)
	{
		return;
	}

	debugLogQueue = xQueueCreateStatic(DEBUG_LOG_QUEUE_LENGTH, sizeof(DebugLogMessage), debugLogQueueStorage,
									   &debugLogQueueControlBlock);

	txHandle = xTaskCreateStatic(debug_log_tx, "debug_log_tx", DEBUG_LOG_TX_STACK_WORDS, NULL,
								 tskIDLE_PRIORITY + 1U, debugLogTxStack, &debugLogTxControlBlock);

	debugLogInitialized = pdTRUE;
}

int debug_log(const char *format, ...)
{
	DebugLogMessage message;
	int formattedLength;
	va_list arguments;

	if ((format == NULL) || (debugLogInitialized == pdFALSE))
	{
		return -1;
	}

	va_start(arguments, format);
	formattedLength = vsnprintf((char *)message.text, sizeof(message.text), format, arguments);
	va_end(arguments);

	if (formattedLength <= 0)
	{
		return (formattedLength < 0) ? -1 : 0;
	}

	if ((uint32_t)formattedLength > DEBUG_LOG_MAX_LENGTH)
	{
		memcpy(&message.text[DEBUG_LOG_MAX_LENGTH - DEBUG_LOG_TRUNCATION_LENGTH], DEBUG_LOG_TRUNCATION_SUFFIX,
			   DEBUG_LOG_TRUNCATION_LENGTH);
		message.length = DEBUG_LOG_MAX_LENGTH;
	}
	else
	{
		message.length = (uint16_t)formattedLength;
	}
	message.text[message.length] = '\0';

	if (xQueueSendToBack(debugLogQueue, &message, 0U) != pdPASS)
	{
		return -1;
	}

	return message.length;
}

static void debug_log_tx(void *argument)
{
	DebugLogMessage message;

	while (1)
	{
		if (xQueueReceive(debugLogQueue, &message, portMAX_DELAY) != pdPASS)
		{
			continue;
		}

		if (debug_log_usb_transmit(&message) == USBD_OK)
		{
			while (debug_log_usb_tx_pending())
			{
				vTaskDelay(1U);
			}
		}
	}
}

static uint8_t debug_log_usb_transmit(DebugLogMessage *message)
{
	uint8_t result = USBD_FAIL;

	taskENTER_CRITICAL();
	if ((hUsbDeviceHS.dev_state == USBD_STATE_CONFIGURED) && (hUsbDeviceHS.pClassData != NULL))
	{
		result = CDC_Transmit_HS(message->text, message->length);
	}
	taskEXIT_CRITICAL();

	return result;
}

static bool debug_log_usb_tx_pending(void)
{
	USBD_CDC_HandleTypeDef *cdcHandle;
	bool pending = false;

	taskENTER_CRITICAL();
	if ((hUsbDeviceHS.dev_state == USBD_STATE_CONFIGURED) && (hUsbDeviceHS.pClassData != NULL))
	{
		cdcHandle = (USBD_CDC_HandleTypeDef *)hUsbDeviceHS.pClassData;
		if (cdcHandle->TxState != 0U)
		{
			pending = true;
		}
	}
	taskEXIT_CRITICAL();

	return pending;
}

int fputc(int c, FILE *stream)
{
	return -1; //暂时不实现
}
