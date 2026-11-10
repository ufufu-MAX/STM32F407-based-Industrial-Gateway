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
#include "uart.h"
#include "W25Q64.h"
#include "can_task.h"
#include "OneNet.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

extern TaskHandle_t Uart_TaskHandle ;
extern TaskHandle_t SPI_TaskHandle ;
extern uint8_t ret;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */



void LED(void *arg)
{
	while(1){
//		if(ret == !HAL_OK){
//		HAL_GPIO_WritePin(GPIOC,GPIO_PIN_13,GPIO_PIN_RESET);
//	vTaskDelay(100);
//	HAL_GPIO_WritePin(GPIOC,GPIO_PIN_13,GPIO_PIN_SET);
//	vTaskDelay(100);
//		}else{		
	HAL_GPIO_WritePin(GPIOC,GPIO_PIN_13,GPIO_PIN_RESET);
	vTaskDelay(500);
	HAL_GPIO_WritePin(GPIOC,GPIO_PIN_13,GPIO_PIN_SET);
	vTaskDelay(500);

	}
}
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

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
  xTaskCreate(W25Q_WritrBINTask,"WriteBINTask",256,NULL,osPriorityBelowNormal,&SPI_TaskHandle);
  xTaskCreate(OTA_Receive_Task,"OTA",128,NULL,osPriorityBelowNormal + 2,&Uart_TaskHandle);
  xTaskCreate(LED,"Task1",128,NULL,osPriorityLow7,NULL);// 呼吸灯任务，优先级最低，测试所有任务是否均被调度
  if(Can_Task_Init(osPriorityBelowNormal + 1, osPriorityBelowNormal) != pdPASS){
    Error_Handler();
  }
  if(OneNet_Task_Init(ONENET_TASK_PRIORITY) != pdPASS){
    Error_Handler();
  }
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
  /* USER CODE BEGIN StartDefaultTask */
	uint8_t confirm_retry;

	// 给各任务一次调度机会后确认新 APP。
	// 最多重试 3 次；始终失败时不伪造确认，下次复位由 Bootloader 回滚。
	for (confirm_retry = 0U; confirm_retry < 3U; confirm_retry++)
	{
		osDelay(1000U);
		if (OTA_ConfirmRunningImage() != 0U)
		{
			break;
		}
	}

  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END StartDefaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

