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
#include "can.h"
#include "dma.h"
#include "spi.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

#include "W25Q64.h"
#include "Flash.h"

#define APPADDRESS     0x08010000U  // APP向量表起点
#define APP_FLASH_END  0x08100000U
#define SRAM_START     0x20000000U
#define SRAM_END       0x20020000U

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef void(*pFunction)(void);
// OTA 数据搬运缓冲区：每次从 W25Q64 读取 2048 字节到这里。
// __align(4) 保证缓冲区地址是 4 字节对齐，才能按 uint32_t 写内部 Flash。
__align(4) uint8_t OTA_Buffer[OTA_COPY_BUFFER_SIZE];

void LED()
{
	HAL_GPIO_WritePin(GPIOC,GPIO_PIN_13,GPIO_PIN_SET);
	HAL_Delay(100);
	HAL_GPIO_WritePin(GPIOC,GPIO_PIN_13,GPIO_PIN_SET);
	HAL_Delay(100);
}
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

// r0：APP 的栈顶地址
// r1：APP 的 Reset_Handler 地址
__asm void APP_Start(uint32_t app_msp, uint32_t app_reset)
{
    MSR MSP, r0    // 切换到 APP 的栈
    CPSIE I        // 重新打开全局中断
    BX r1          // 跳到 APP 的 Reset_Handler
}

void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
		if(hcan->Instance == CAN1){
			
	}
}

void Jump_To_APP(void)
{
    uint32_t app_msp;
    uint32_t app_reset;
    uint32_t reset_address;
    uint32_t index;

    // APP 向量表第一个数据：栈顶地址 MSP
    // APP 向量表第二个数据：Reset_Handler 地址
    app_msp = *(__IO uint32_t *)APPADDRESS;
    app_reset = *(__IO uint32_t *)(APPADDRESS + 4U);

    // Cortex-M 函数地址最低位是 Thumb 标志，
    // 检查地址时先去掉最低位。
    reset_address = app_reset & ~1U;

    // 检查 APP 栈顶是否位于 STM32F407 的 SRAM
    if ((app_msp < SRAM_START) ||
        (app_msp > SRAM_END) ||
        ((app_msp & 0x7U) != 0U))
    {
        return;
    }

    // APP 入口函数最低位必须是 1，
    // 并且入口必须位于 APP 的 Flash 区域。
    if (((app_reset & 1U) == 0U) ||
        (reset_address < APPADDRESS) ||
        (reset_address >= APP_FLASH_END))
    {
        return;
    }

    // 复位 Bootloader 使用过的外设。
    // 例如 GPIO、DMA、CAN、UART、SPI。
    HAL_DeInit();

    // 把系统时钟恢复到复位状态：
    // HSI 作为系统时钟，关闭 PLL。
    // 这里必须放在 __disable_irq() 和关闭 SysTick 之前，
    // 因为 HAL_RCC_DeInit() 内部可能使用 HAL_GetTick() 进行超时判断。
    if (HAL_RCC_DeInit() != HAL_OK)
    {
        return;
    }

    // 清理中断期间先关闭总中断。
    // 最后会在 APP_Start() 中重新打开。
    __disable_irq();

    // 停止并清空 SysTick
    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL  = 0U;

    // 清除正在等待的 SysTick 和 PendSV。
    // 只关闭 SysTick 不够，因为它可能已经产生了一个
    // 等待处理的中断。
	
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk |
                SCB_ICSR_PENDSVCLR_Msk;
				

    // ICER：关闭所有外部中断。
    // ICPR：清除所有正在等待的外部中断。
    // 这里会清理 Bootloader 遗留的 CAN、DMA、
    // UART 等 NVIC 状态。
    for (index = 0U;
         index < (sizeof(NVIC->ICER) / sizeof(NVIC->ICER[0]));
         index++)
    {
        NVIC->ICER[index] = 0xFFFFFFFFU;
        NVIC->ICPR[index] = 0xFFFFFFFFU;
    }

    // 把中断向量表切换到 APP。
    // 不设置它的话，APP 发生中断时，
    // CPU 可能还会使用 Bootloader 的中断函数。
    SCB->VTOR = APPADDRESS;

    // 清除 Bootloader 可能留下的其他中断屏蔽状态。
    __set_BASEPRI(0U);
    __set_FAULTMASK(0U);
    __set_CONTROL(0U);

    // 确保前面的寄存器操作真正生效后再跳转。
    __DSB();
    __ISB();

    // 设置 APP 栈顶，重新打开中断，
    // 直接进入 APP Reset_Handler。
    APP_Start(app_msp, app_reset);

    // 正常情况下不会执行到这里
    while (1)
    {
		LED();
    }
}

// void Jump_To_APP()
// {
//	uint32_t Jumpaddress;
//	pFunction JumpToAPPLoction;
//	
//	if(((*(__IO uint32_t *)(APPADDRESS)) & 0x2FFE0000) == 0x20000000){
//		Jumpaddress = *(__IO uint32_t *)(APPADDRESS + 4);
//		JumpToAPPLoction = (pFunction)Jumpaddress;
//		HAL_DeInit();
//		SysTick->CTRL = 0;
//		SysTick->LOAD = 0;
//		SysTick->VAL = 0;
//		__disable_irq();
//		
//		__set_MSP(*(__IO uint32_t *)(APPADDRESS));
//		
//		JumpToAPPLoction();
//	}
// }

void CAN_Filter_Config()
{
	CAN_FilterTypeDef sFilterConfig;
	
	sFilterConfig.FilterBank = 0;
    sFilterConfig.FilterMode = CAN_FILTERMODE_IDMASK;
    sFilterConfig.FilterScale = CAN_FILTERSCALE_32BIT;
    sFilterConfig.FilterIdHigh = 0x0000;
    sFilterConfig.FilterIdLow = 0x0000;
    sFilterConfig.FilterMaskIdHigh = 0x0000;
    sFilterConfig.FilterMaskIdLow = 0x0000;
    sFilterConfig.FilterFIFOAssignment = CAN_RX_FIFO0;
    sFilterConfig.FilterActivation = ENABLE;
    sFilterConfig.SlaveStartFilterBank = 14;
	
	if((HAL_CAN_ConfigFilter(&hcan1,&sFilterConfig)) != HAL_OK){
		Error_Handler();
		
	}
	if((HAL_CAN_Start(&hcan1)) != HAL_OK){
		Error_Handler();
	}
	
	if((HAL_CAN_ActivateNotification(&hcan1,CAN_IT_RX_FIFO0_MSG_PENDING)) != HAL_OK){
		Error_Handler();
	}
	
}
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

static uint8_t OTA_AppVectorIsValid(void)
{
	uint32_t app_msp = *(__IO uint32_t *)APPADDRESS;
	uint32_t app_reset = *(__IO uint32_t *)(APPADDRESS + 4U);
	uint32_t reset_address = app_reset & ~1U;

	if ((app_msp < SRAM_START) || (app_msp > SRAM_END) ||
		((app_msp & 0x7U) != 0U))
	{
		return 0U;
	}

	if (((app_reset & 1U) == 0U) ||
		(reset_address < APPADDRESS) ||
		(reset_address >= APP_FLASH_END))
	{
		return 0U;
	}

	return 1U;
}

// 仅在内部旧 APP 仍完整时使用：取消本次升级并保留当前 APP。
static uint8_t OTA_CancelUpdate(void)
{
	// 先清升级请求，再清状态；任一步掉电，下一次启动仍会重复安全清理。
	if (W25Q_EraseSector(W25Q64_OTA_Data_ADD) == 0U)
	{
		return 0U;
	}
	return OTA_StateReset();
}

// 进入 ROLLBACK 后，即使恢复过程中再次掉电，下一次启动也会重新恢复，
// 直到内部 Flash 的整包 CRC 与备份一致。
static uint8_t OTA_PerformRollback(uint32_t current_state)
{
	if ((current_state != OTA_STATE_ROLLBACK) &&
		(OTA_StateAppend(OTA_STATE_ROLLBACK) == 0U))
	{
		return 0U;
	}

	if (OTA_RestoreBackup() == 0U)
	{
		return 0U;
	}

	if (W25Q_EraseSector(W25Q64_OTA_Data_ADD) == 0U)
	{
		return 0U;
	}

	return OTA_StateAppend(OTA_STATE_ROLLBACK_DONE);
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  // 保存 OTA_Data_Read() 解析出的 BIN 大小和固件版本号。
	OTA_Info_t ota_info;
	uint32_t ota_boot_state = OTA_STATE_NONE;

  /* USER CODE END 1 */

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
  MX_DMA_Init();
  MX_CAN1_Init();
  MX_SPI1_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
	// 状态日志读取失败时不执行任何擦除，优先尝试保留当前 APP。
	if (OTA_StateRead(&ota_boot_state) == 0U)
	{
		Jump_To_APP();
		while (1) { LED(); }
	}

	// 新 APP 已确认：清理旧事务状态，然后仍允许处理下一次新升级。
	if (ota_boot_state == OTA_STATE_CONFIRMED)
	{
		if ((OTA_VersionConfirmPending() == 0U) ||
			(OTA_StateReset() == 0U))
		{
			while (1) { LED(); }
		}
		ota_boot_state = OTA_STATE_NONE;
	}

	// 回滚已经完成：先清升级请求，再清状态，随后运行恢复出的旧 APP。
	if (ota_boot_state == OTA_STATE_ROLLBACK_DONE)
	{
		if (OTA_CancelUpdate() != 0U)
		{
			Jump_To_APP();
		}
		while (1) { LED(); }
	}

	// 备份阶段掉电时内部 APP 尚未擦除，取消升级并直接保留旧 APP。
	if ((ota_boot_state == OTA_STATE_BACKUPING) ||
		(ota_boot_state == OTA_STATE_BACKUP_READY))
	{
		if (OTA_CancelUpdate() != 0U)
		{
			Jump_To_APP();
		}
		while (1) { LED(); }
	}

	// 第一次试运行新 APP：先记录 BOOTING，若确认前再次复位就回滚。
	if (ota_boot_state == OTA_STATE_PENDING_CONFIRM)
	{
		if ((OTA_AppVectorIsValid() != 0U) &&
			(OTA_StateAppend(OTA_STATE_BOOTING) != 0U))
		{
			Jump_To_APP();
		}

		if (OTA_PerformRollback(OTA_STATE_PENDING_CONFIRM) != 0U)
		{
			NVIC_SystemReset();
		}
		while (1) { LED(); }
	}

	// 安装、试运行或回滚过程中掉电：始终从有效备份重新恢复旧 APP。
	if ((ota_boot_state == OTA_STATE_INSTALLING) ||
		(ota_boot_state == OTA_STATE_BOOTING) ||
		(ota_boot_state == OTA_STATE_ROLLBACK))
	{
		if (OTA_PerformRollback(ota_boot_state) != 0U)
		{
			NVIC_SystemReset();
		}
		while (1) { LED(); }
	}

	// 没有未完成事务时，才允许识别并开始一次全新的升级。
	if ((ota_boot_state == OTA_STATE_NONE) &&
		(OTA_Data_Read(&ota_info) != 0U))
	{
		// 擦状态扇区发生在内部旧 APP 仍完整的时候，掉电也可以安全重来。
		if ((OTA_StateReset() == 0U) ||
			(OTA_StateAppend(OTA_STATE_BACKUPING) == 0U))
		{
			while (1) { LED(); }
		}

		// 备份失败时内部 APP 未被擦除，清理请求后继续运行旧 APP。
		if ((OTA_BackupCurrentApp() == 0U) ||
			(OTA_StateAppend(OTA_STATE_BACKUP_READY) == 0U) ||
			(OTA_StateAppend(OTA_STATE_INSTALLING) == 0U))
		{
			if (OTA_CancelUpdate() != 0U)
			{
				Jump_To_APP();
			}
			while (1) { LED(); }
		}

		// INSTALLING 已经落盘；此后的任何失败都必须走回滚，不能直接跳 APP。
		if ((OTA_CopyToFlash(&ota_info) == 0U) ||
			(OTA_VerifyInternalFlashCrc32(OTA_APP_ADDRESS,
											ota_info.bin_size,
											ota_info.image_crc32) == 0U) ||
			(W25Q_EraseSector(W25Q64_OTA_Data_ADD) == 0U) ||
			(OTA_StateAppend(OTA_STATE_PENDING_CONFIRM) == 0U))
		{
			if (OTA_PerformRollback(OTA_STATE_INSTALLING) != 0U)
			{
				NVIC_SystemReset();
			}
			while (1) { LED(); }
		}

		// 下一次 Bootloader 启动会给新 APP 一次确认机会。
		NVIC_SystemReset();
	}

  Jump_To_APP();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
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

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 168;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

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
